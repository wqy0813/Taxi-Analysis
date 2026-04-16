import {
    state,
    DENSITY_CHUNK_CACHE_LIMIT,
    DENSITY_CHUNK_PREFETCH_MARGIN
} from "../../core/state.js";
import { qs, setText, renderInfoPanel, formatDateTime, requestArrow } from "../../core/utils.js";
import { pointToOverlayPixel, requestDensityRedraw, cancelDensityRedraw, clearDensityOverlay } from "../../map/map.js";
import { closeDensityTrendModal, clearDensityTrendPreview, clearDensityTrendModal, ensureDensityTrendCharts, renderDensityTrendModal, buildDensityPreviewOption } from "./densityChart.js";

function clearDensityChunkCache() {
    state.densityChunkCache.clear();
    state.densityChunkAccessTick = 0;
}

function hideDensityTooltip() {
    if (!state.densityTooltip) {
        return;
    }
    state.densityTooltip.style.display = "none";
}

function showDensityTooltip(html, x, y) {
    if (!state.densityTooltip) {
        return;
    }
    state.densityTooltip.innerHTML = html;
    state.densityTooltip.style.left = `${x + 12}px`;
    state.densityTooltip.style.top = `${y + 12}px`;
    state.densityTooltip.style.display = "block";
}

function clearDensityQueryState() {
    if (state.densityPlayTimer) {
        clearInterval(state.densityPlayTimer);
        state.densityPlayTimer = null;
    }
    state.currentBucketIndex = 0;
    state.densityHoverCell = null;
    state.densitySelectedCellKey = null;
    cancelDensityRedraw();
    clearDensityChunkCache();
    closeDensityTrendModal();
    qs("density-bucket").innerHTML = "";
    const timeline = qs("density-timeline");
    if (timeline) {
        timeline.min = "0";
        timeline.max = "0";
        timeline.value = "0";
    }
    const playButton = qs("density-play");
    if (playButton) {
        playButton.textContent = "播放";
    }
    setText("density-time-label", "-");
    renderInfoPanel("density-trend-summary", [], "点击网格查看趋势");
    hideDensityTooltip();
    clearDensityTrendPreview();
    clearDensityTrendModal();
    clearDensityOverlay();
}

function clearDensityCacheOnly() {
    state.densityMeta = null;
    state.densityQueryId = null;
    state.densityBucketCache.clear();
    state.densityCellMaps.clear();
    state.densityChunkMaps.clear();
    state.densityTrendCache.clear();
    state.densityQueryParams = null;
    state.densityRenderModel = null;
}

function resetDensityState() {
    clearDensityQueryState();
    clearDensityCacheOnly();
}

function getDensityGridModel() {
    if (!state.densityMeta) return null;

    return {
        minLon: state.densityMeta.minLon,
        minLat: state.densityMeta.minLat,
        maxLon: state.densityMeta.maxLon,
        maxLat: state.densityMeta.maxLat,
        lonStep: state.densityMeta.lonStep,
        latStep: state.densityMeta.latStep,
        columnCount: state.densityMeta.columnCount,
        rowCount: state.densityMeta.rowCount
    };
}

function buildDensityChunkMaps() {
    state.densityChunkMaps = new Map();
    clearDensityChunkCache();
}

function getDensityCellBoundsFromGrid(gx, gy, grid = getDensityGridModel()) {
    if (!grid) {
        return null;
    }

    const cellMinLon = grid.minLon + gx * grid.lonStep;
    const cellMinLat = grid.minLat + gy * grid.latStep;
    return {
        minLon: cellMinLon,
        minLat: cellMinLat,
        maxLon: Math.min(cellMinLon + grid.lonStep, grid.maxLon),
        maxLat: Math.min(cellMinLat + grid.latStep, grid.maxLat)
    };
}

function getDensityChunkCells(bucketIndex, chunkKey) {
    return state.densityChunkMaps.get(bucketIndex)?.get(chunkKey) || null;
}

function getDensityChunkCacheKey(bucketIndex, zoom, dpr, chunkKey) {
    return `${bucketIndex}|${zoom}|${dpr}|${chunkKey}`;
}

function getDensityChunkRange() {
    const grid = getDensityGridModel();
    if (!grid || !state.map || typeof state.map.getBounds !== "function") {
        return null;
    }

    const bounds = state.map.getBounds();
    if (!bounds || typeof bounds.getSouthWest !== "function" || typeof bounds.getNorthEast !== "function") {
        return null;
    }

    const sw = bounds.getSouthWest();
    const ne = bounds.getNorthEast();
    const viewMinLon = Math.max(grid.minLon, sw.lng);
    const viewMaxLon = Math.min(grid.maxLon, ne.lng);
    const viewMinLat = Math.max(grid.minLat, sw.lat);
    const viewMaxLat = Math.min(grid.maxLat, ne.lat);
    if (viewMinLon >= viewMaxLon || viewMinLat >= viewMaxLat) {
        return null;
    }

    const minGx = Math.max(0, Math.floor((viewMinLon - grid.minLon) / grid.lonStep));
    const maxGx = Math.min(grid.columnCount - 1, Math.ceil((viewMaxLon - grid.minLon) / grid.lonStep) - 1);
    const minGy = Math.max(0, Math.floor((viewMinLat - grid.minLat) / grid.latStep));
    const maxGy = Math.min(grid.rowCount - 1, Math.ceil((viewMaxLat - grid.minLat) / grid.latStep) - 1);
    if (maxGx < minGx || maxGy < minGy) {
        return null;
    }

    const minChunkX = Math.max(0, Math.floor(minGx / grid.chunkCellSize) - DENSITY_CHUNK_PREFETCH_MARGIN);
    const maxChunkX = Math.min(grid.chunkColumnCount - 1, Math.floor(maxGx / grid.chunkCellSize) + DENSITY_CHUNK_PREFETCH_MARGIN);
    const minChunkY = Math.max(0, Math.floor(minGy / grid.chunkCellSize) - DENSITY_CHUNK_PREFETCH_MARGIN);
    const maxChunkY = Math.min(grid.chunkRowCount - 1, Math.floor(maxGy / grid.chunkCellSize) + DENSITY_CHUNK_PREFETCH_MARGIN);

    return {
        minChunkX,
        maxChunkX,
        minChunkY,
        maxChunkY
    };
}

function getDensityVisibleChunks() {
    const grid = getDensityGridModel();
    const range = getDensityChunkRange();
    if (!grid || !range || !grid.chunkLookup) {
        return [];
    }

    const chunks = [];
    for (let chunkY = range.minChunkY; chunkY <= range.maxChunkY; chunkY += 1) {
        for (let chunkX = range.minChunkX; chunkX <= range.maxChunkX; chunkX += 1) {
            const chunk = grid.chunkLookup.get(`${chunkX}:${chunkY}`);
            if (chunk) {
                chunks.push(chunk);
            }
        }
    }
    return chunks;
}

function evictDensityChunkCacheIfNeeded() {
    if (state.densityChunkCache.size <= DENSITY_CHUNK_CACHE_LIMIT) {
        return;
    }

    const entries = Array.from(state.densityChunkCache.entries());
    entries.sort((a, b) => {
        const scoreA = (a[1].useCount * 10) + a[1].lastUsedTick;
        const scoreB = (b[1].useCount * 10) + b[1].lastUsedTick;
        return scoreA - scoreB;
    });

    while (state.densityChunkCache.size > DENSITY_CHUNK_CACHE_LIMIT && entries.length > 0) {
        const [key] = entries.shift();
        state.densityChunkCache.delete(key);
    }
}

function renderDensityChunkToCanvas(ctx, grid, bucketIndex, chunk) {
    const chunkCells = getDensityChunkCells(bucketIndex, chunk.key);
    const chunkTopLeft = pointToOverlayPixel(chunk.minLon, chunk.maxLat);
    const chunkBottomRight = pointToOverlayPixel(chunk.maxLon, chunk.minLat);
    const width = Math.max(1, Math.ceil(Math.abs(chunkBottomRight.x - chunkTopLeft.x)));
    const height = Math.max(1, Math.ceil(Math.abs(chunkBottomRight.y - chunkTopLeft.y)));
    const cellMap = chunkCells || new Map();

    ctx.save();
    ctx.lineJoin = "miter";
    ctx.lineCap = "square";
    const baseStrokeStyle = "rgba(136, 163, 183, 0.28)";
    ctx.lineWidth = 1;

    for (let gy = chunk.minGy; gy <= chunk.maxGy; gy += 1) {
        for (let gx = chunk.minGx; gx <= chunk.maxGx; gx += 1) {
            const bounds = getDensityCellBoundsFromGrid(gx, gy, grid);
            if (!bounds) {
                continue;
            }

            const topLeft = pointToOverlayPixel(bounds.minLon, bounds.maxLat);
            const bottomRight = pointToOverlayPixel(bounds.maxLon, bounds.minLat);
            const centerX = (topLeft.x + bottomRight.x) * 0.5;
            const centerY = (topLeft.y + bottomRight.y) * 0.5;
            const drawWidth = Math.max(1, Math.abs(bottomRight.x - topLeft.x));
            const drawHeight = Math.max(1, Math.abs(bottomRight.y - topLeft.y));
            const left = Math.round(centerX - drawWidth * 0.5);
            const top = Math.round(centerY - drawHeight * 0.5);
            const localLeft = left - chunkTopLeft.x;
            const localTop = top - chunkTopLeft.y;

            ctx.strokeStyle = baseStrokeStyle;
            ctx.strokeRect(localLeft + 0.5, localTop + 0.5, Math.max(0, Math.round(drawWidth) - 1), Math.max(0, Math.round(drawHeight) - 1));

            const cell = cellMap.get(`${gx}:${gy}`);
            if (cell) {
                const cellColor = getHeatColor(cell);
                ctx.fillStyle = cellColor;
                ctx.fillRect(localLeft, localTop, drawWidth, drawHeight);
            }
        }
    }

    ctx.restore();
    return { width, height, topLeft: chunkTopLeft };
}

function getDensityChunkRender(bucketIndex, chunk) {
    const grid = getDensityGridModel();
    if (!grid || !state.map || !state.densityOverlay) {
        return null;
    }

    const zoom = state.map.getZoom();
    const dpr = state.densityOverlay._dpr || window.devicePixelRatio || 1;
    const cacheKey = getDensityChunkCacheKey(bucketIndex, zoom, dpr, chunk.key);
    const existing = state.densityChunkCache.get(cacheKey);
    if (existing) {
        existing.useCount += 1;
        existing.lastUsedTick = ++state.densityChunkAccessTick;
        return existing;
    }

    const chunkTopLeft = pointToOverlayPixel(chunk.minLon, chunk.maxLat);
    const chunkBottomRight = pointToOverlayPixel(chunk.maxLon, chunk.minLat);
    const cssWidth = Math.max(1, Math.ceil(Math.abs(chunkBottomRight.x - chunkTopLeft.x)));
    const cssHeight = Math.max(1, Math.ceil(Math.abs(chunkBottomRight.y - chunkTopLeft.y)));
    const canvas = document.createElement("canvas");
    const pixelWidth = Math.max(1, Math.round(cssWidth * dpr));
    const pixelHeight = Math.max(1, Math.round(cssHeight * dpr));
    canvas.width = pixelWidth;
    canvas.height = pixelHeight;
    canvas.style.width = `${cssWidth}px`;
    canvas.style.height = `${cssHeight}px`;

    const ctx = canvas.getContext("2d");
    if (!ctx) {
        return null;
    }
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, cssWidth, cssHeight);
    renderDensityChunkToCanvas(ctx, grid, bucketIndex, chunk);

    const entry = {
        canvas,
        width: cssWidth,
        height: cssHeight,
        useCount: 1,
        lastUsedTick: ++state.densityChunkAccessTick
    };
    state.densityChunkCache.set(cacheKey, entry);
    evictDensityChunkCacheIfNeeded();
    return entry;
}

function parseDensityCellKey(key) {
    const parts = String(key || "").split(":");
    if (parts.length !== 2) {
        return null;
    }

    const gx = Number(parts[0]);
    const gy = Number(parts[1]);
    if (!Number.isInteger(gx) || !Number.isInteger(gy)) {
        return null;
    }
    return { gx, gy };
}

function getDensityCellByKey(bucketIndex, key) {
    const bucket = state.densityCellMaps.get(bucketIndex);
    const keyText = String(key || "");
    const cached = bucket?.get(keyText);
    if (cached) {
        return cached;
    }

    const grid = parseDensityCellKey(keyText);
    if (!grid) {
        return null;
    }

    return {
        gx: grid.gx,
        gy: grid.gy,
        pointCount: 0,
        vehicleDensity: 0,
        flowIntensity: 0,
        deltaVehicleDensity: 0,
        deltaRate: 0
    };
}

function getDensityCellBounds(cell) {
    if (!state.densityMeta || !cell) {
        return null;
    }

    const grid = getDensityGridModel();
    if (grid && Number.isInteger(Number(cell.gx)) && Number.isInteger(Number(cell.gy))) {
        return getDensityCellBoundsFromGrid(Number(cell.gx), Number(cell.gy), grid);
    }

    return null;
}

function drawDensityGridBase(ctx, visibleRange) {
    if (!state.densityMeta) {
        return;
    }

    const minLon = Number(state.densityMeta.minLon);
    const minLat = Number(state.densityMeta.minLat);
    const maxLon = Number(state.densityMeta.maxLon);
    const maxLat = Number(state.densityMeta.maxLat);
    const lonStep = Number(state.densityMeta.lonStep);
    const latStep = Number(state.densityMeta.latStep);
    const columnCount = Number(state.densityMeta.columnCount || 0);
    const rowCount = Number(state.densityMeta.rowCount || 0);

    if (!Number.isFinite(minLon) || !Number.isFinite(minLat) ||
        !Number.isFinite(maxLon) || !Number.isFinite(maxLat) ||
        !Number.isFinite(lonStep) || !Number.isFinite(latStep) ||
        lonStep <= 0 || latStep <= 0 || columnCount <= 0 || rowCount <= 0) {
        return;
    }

    const range = visibleRange || {
        minGx: 0,
        maxGx: columnCount - 1,
        minGy: 0,
        maxGy: rowCount - 1
    };

    ctx.save();
    ctx.strokeStyle = "rgba(136, 163, 183, 0.28)";
    ctx.lineWidth = 1;
    ctx.fillStyle = "rgba(255, 255, 255, 0)";

    for (let gy = range.minGy; gy <= range.maxGy; gy += 1) {
        for (let gx = range.minGx; gx <= range.maxGx; gx += 1) {
            const cellMinLon = minLon + gx * lonStep;
            const cellMinLat = minLat + gy * latStep;
            const cellMaxLon = Math.min(cellMinLon + lonStep, maxLon);
            const cellMaxLat = Math.min(cellMinLat + latStep, maxLat);
            const topLeft = pointToOverlayPixel(cellMinLon, cellMaxLat);
            const bottomRight = pointToOverlayPixel(cellMaxLon, cellMinLat);
            const centerX = (topLeft.x + bottomRight.x) * 0.5;
            const centerY = (topLeft.y + bottomRight.y) * 0.5;
            const drawWidth = Math.max(1, Math.abs(bottomRight.x - topLeft.x));
            const drawHeight = Math.max(1, Math.abs(bottomRight.y - topLeft.y));
            const left = Math.round(centerX - drawWidth * 0.5);
            const top = Math.round(centerY - drawHeight * 0.5);
            ctx.strokeRect(left + 0.5, top + 0.5, Math.max(0, Math.round(drawWidth) - 1), Math.max(0, Math.round(drawHeight) - 1));
        }
    }

    ctx.restore();
}

function drawDensityHeatCells(ctx, cells, visibleRange) {
    if (!cells || cells.length === 0) {
        return;
    }

    ctx.save();
    ctx.globalCompositeOperation = "source-over";
    ctx.lineJoin = "miter";
    ctx.lineCap = "square";

    for (const cell of cells) {
        if (visibleRange) {
            const gx = Number(cell.gx || 0);
            const gy = Number(cell.gy || 0);
            if (gx < visibleRange.minGx || gx > visibleRange.maxGx || gy < visibleRange.minGy || gy > visibleRange.maxGy) {
                continue;
            }
        }

        const bounds = getDensityCellBounds(cell);
        if (!bounds) {
            continue;
        }

        const topLeft = pointToOverlayPixel(bounds.minLon, bounds.maxLat);
        const bottomRight = pointToOverlayPixel(bounds.maxLon, bounds.minLat);
        const centerX = (topLeft.x + bottomRight.x) * 0.5;
        const centerY = (topLeft.y + bottomRight.y) * 0.5;
        const rawWidth = Math.abs(bottomRight.x - topLeft.x);
        const rawHeight = Math.abs(bottomRight.y - topLeft.y);
        const drawWidth = Math.max(1, rawWidth);
        const drawHeight = Math.max(1, rawHeight);
        const left = Math.round(centerX - drawWidth * 0.5);
        const top = Math.round(centerY - drawHeight * 0.5);

        const cellColor = getHeatColor(cell);
        ctx.fillStyle = cellColor;
        ctx.fillRect(left, top, drawWidth, drawHeight);
    }

    ctx.restore();
}

function getVisibleDensityGridRange() {
    if (!state.densityMeta || !state.map || typeof state.map.getBounds !== "function") {
        return null;
    }

    const bounds = state.map.getBounds();
    if (!bounds || typeof bounds.getSouthWest !== "function" || typeof bounds.getNorthEast !== "function") {
        return null;
    }

    const sw = bounds.getSouthWest();
    const ne = bounds.getNorthEast();
    const minLon = Number(state.densityMeta.minLon);
    const minLat = Number(state.densityMeta.minLat);
    const maxLon = Number(state.densityMeta.maxLon);
    const maxLat = Number(state.densityMeta.maxLat);
    const lonStep = Number(state.densityMeta.lonStep);
    const latStep = Number(state.densityMeta.latStep);
    const columnCount = Number(state.densityMeta.columnCount || 0);
    const rowCount = Number(state.densityMeta.rowCount || 0);

    if (!Number.isFinite(minLon) || !Number.isFinite(minLat) ||
        !Number.isFinite(maxLon) || !Number.isFinite(maxLat) ||
        !Number.isFinite(lonStep) || !Number.isFinite(latStep) ||
        lonStep <= 0 || latStep <= 0 || columnCount <= 0 || rowCount <= 0) {
        return null;
    }

    const viewMinLon = Math.max(minLon, sw.lng);
    const viewMaxLon = Math.min(maxLon, ne.lng);
    const viewMinLat = Math.max(minLat, sw.lat);
    const viewMaxLat = Math.min(maxLat, ne.lat);
    if (viewMinLon >= viewMaxLon || viewMinLat >= viewMaxLat) {
        return null;
    }

    const minGx = Math.max(0, Math.floor((viewMinLon - minLon) / lonStep));
    const maxGx = Math.min(columnCount - 1, Math.ceil((viewMaxLon - minLon) / lonStep) - 1);
    const minGy = Math.max(0, Math.floor((viewMinLat - minLat) / latStep));
    const maxGy = Math.min(rowCount - 1, Math.ceil((viewMaxLat - minLat) / latStep) - 1);
    if (maxGx < minGx || maxGy < minGy) {
        return null;
    }

    return {
        minGx: Math.max(0, minGx - 1),
        maxGx: Math.min(columnCount - 1, maxGx + 1),
        minGy: Math.max(0, minGy - 1),
        maxGy: Math.min(rowCount - 1, maxGy + 1)
    };
}

function drawDensityBucket() {
    requestDensityRedraw();
}

function getDensityRatio(cell) {
    if (!state.densityMeta) {
        return 0;
    }
    const maxDensity = Number(state.densityMeta.maxVehicleDensity || 0);
    if (maxDensity <= 0) {
        return 0;
    }
    const density = Number(cell?.vehicleDensity || 0);
    return Math.max(0, Math.min(1, density / maxDensity));
}

function mixColor(start, end, t) {
    const tt = Math.max(0, Math.min(1, t));
    const r = Math.round(start[0] + (end[0] - start[0]) * tt);
    const g = Math.round(start[1] + (end[1] - start[1]) * tt);
    const b = Math.round(start[2] + (end[2] - start[2]) * tt);
    return [r, g, b];
}

function getHeatColor(cell) {
    const d = Number(cell?.vehicleDensity || 0);

    let color;
    if (d < 5) {
        color = "rgba(100, 180, 255, 0.3)";
    } else if (d < 20) {
        color = "rgba(255, 140, 60, 0.4)";
    } else {
        color = "rgba(220, 60, 60, 0.5)";
    }
    return color;
}

function getCurrentBucket() {
    return state.densityBucketCache.get(state.currentBucketIndex) || null;
}

function trimDensityBucketCache(center) {
    const keep = new Set();
    for (let i = center - 2; i <= center + 2; i++) {
        if (i >= 0 && state.densityMeta && i < state.densityMeta.bucketCount) {
            keep.add(i);
        }
    }

    for (const k of state.densityBucketCache.keys()) {
        if (!keep.has(k)) {
            state.densityBucketCache.delete(k);
            state.densityCellMaps.delete(k);
        }
    }
}

async function loadCellTrend(cellKey) {
    if (state.densityTrendCache.has(cellKey)) {
        return state.densityTrendCache.get(cellKey);
    }

    const [gx, gy] = cellKey.split(":").map(Number);

    const rows = await requestArrow("/api/density/cell-trend", {
        method: "POST",
        body: JSON.stringify({
            queryId: state.densityQueryId,
            gx,
            gy
        })
    });

    const sortedRows = rows.slice().sort((a, b) => Number(a.bucketIndex) - Number(b.bucketIndex));
    const cells = sortedRows.map((item) => ({
        gx,
        gy,
        seconds: Number(item.seconds || 0),
        vehicleDensity: (Number(item.seconds || 0) / state.densityMeta.bucketSeconds) / state.densityMeta.cellAreaKm2,
        pointCount: 0
    }));

    const data = {
        labels: sortedRows.map((item) => getBucketShortLabel({ startTime: Number(item.startTime) })),
        fullLabels: sortedRows.map((item) => {
            const startTime = Number(item.startTime);
            const endTime = Number(item.endTime);
            return `${formatDateTime(startTime)} - ${formatDateTime(endTime)}`;
        }),
        densityValues: cells.map(c => c.vehicleDensity),
        cells,
        hasData: cells.some(c => Number(c.seconds || 0) > 0)
    };

    state.densityTrendCache.set(cellKey, data);
    return data;
}

function prefetchDensityBuckets(center) {
    [center + 1, center - 1].forEach(i => {
        if (state.densityMeta && i >= 0 && i < state.densityMeta.bucketCount &&
            !state.densityBucketCache.has(i)) {
            loadDensityBucketInBackground(i);
        }
    });
}

async function loadDensityBucketInBackground(bucketIndex) {
    if (state.densityBucketCache.has(bucketIndex)) {
        return;
    }

    try {
        const rows = await requestArrow("/api/density/bucket", {
            method: "POST",
            body: JSON.stringify({
                queryId: state.densityQueryId,
                bucketIndex
            })
        });

        const meta = state.densityMeta;
        const cells = rows.map(({ gx, gy, seconds }) => {
            const gxNum = Number(gx);
            const gyNum = Number(gy);
            const secondsNum = Number(seconds || 0);
            const density = (secondsNum / meta.bucketSeconds) / meta.cellAreaKm2;
            return { gx: gxNum, gy: gyNum, seconds: secondsNum, vehicleDensity: density };
        });

        const summary = meta?.buckets?.[bucketIndex];
        const startTime = Number(summary?.startTime ?? (meta.startTime + bucketIndex * meta.bucketSeconds));
        const endTime = Number(summary?.endTime ?? Math.min(startTime + meta.bucketSeconds - 1, meta.endTime));
        const bucketData = { bucketIndex, startTime, endTime, cells };
        state.densityBucketCache.set(bucketIndex, bucketData);

        const map = new Map();
        cells.forEach(c => map.set(`${c.gx}:${c.gy}`, c));
        state.densityCellMaps.set(bucketIndex, map);

        trimDensityBucketCache(bucketIndex);
    } catch (error) {
    }
}

function getBucketLabel(bucket) {
    if (!bucket) {
        return "-";
    }
    return `${formatDateTime(bucket.startTime)} - ${formatDateTime(bucket.endTime)}`;
}

function getBucketShortLabel(bucket) {
    if (!bucket) {
        return "-";
    }
    const startText = String(formatDateTime(bucket.startTime) || "-");
    const match = startText.match(/(\d{2}:\d{2})/);
    if (match) {
        return match[1];
    }
    return startText;
}

function buildDensityCellMaps() {
    if (!state.densityMeta) {
        state.densityCellMaps = new Map();
        return;
    }
    const currentBucket = state.densityBucketCache.get(state.currentBucketIndex);
    if (currentBucket?.cells) {
        const map = new Map();
        for (const cell of currentBucket.cells) {
            map.set(`${cell.gx}:${cell.gy}`, cell);
        }
        state.densityCellMaps.set(state.currentBucketIndex, map);
    }
}

function updateDensityTimeLabel() {
    const bucket = getCurrentBucket();
    setText("density-time-label", getBucketLabel(bucket));
}

async function setDensityBucketIndex(index) {
    const count = state.densityMeta.bucketCount;
    const next = ((index % count) + count) % count;

    await ensureDensityBucketLoaded(next);

    state.currentBucketIndex = next;

    qs("density-bucket").value = String(next);
    qs("density-timeline").value = String(next);

    updateDensityTimeLabel();
    drawDensityBucket();
}

async function ensureDensityBucketLoaded(bucketIndex) {
    if (state.densityBucketCache.has(bucketIndex)) {
        return state.densityBucketCache.get(bucketIndex);
    }

    const rows = await requestArrow("/api/density/bucket", {
        method: "POST",
        body: JSON.stringify({
            queryId: state.densityQueryId,
            bucketIndex
        })
    });

    const meta = state.densityMeta;
    const cells = rows.map(({ gx, gy, seconds }) => {
        const gxNum = Number(gx);
        const gyNum = Number(gy);
        const secondsNum = Number(seconds || 0);
        const density = (secondsNum / meta.bucketSeconds) / meta.cellAreaKm2;
        return {
            gx: gxNum,
            gy: gyNum,
            seconds: secondsNum,
            vehicleDensity: density
        };
    });

    const summary = meta?.buckets?.[bucketIndex];
    const startTime = Number(summary?.startTime ?? (meta.startTime + bucketIndex * meta.bucketSeconds));
    const endTime = Number(summary?.endTime ?? Math.min(startTime + meta.bucketSeconds - 1, meta.endTime));

    const bucketData = {
        bucketIndex,
        startTime,
        endTime,
        cells
    };

    state.densityBucketCache.set(bucketIndex, bucketData);

    const map = new Map();
    cells.forEach(c => map.set(`${c.gx}:${c.gy}`, c));
    state.densityCellMaps.set(bucketIndex, map);

    trimDensityBucketCache(bucketIndex);
    prefetchDensityBuckets(bucketIndex);

    return bucketData;
}

async function renderSelectedCellTrend() {
    ensureDensityTrendCharts();
    if (!state.densityTrendPreviewChart) {
        return;
    }

    if (!state.densityMeta || !state.densitySelectedCellKey) {
        clearDensityTrendPreview();
        if (state.densityTrendModalOpen) {
            clearDensityTrendModal();
        }
        return;
    }

    try {
        const trend = await loadCellTrend(state.densitySelectedCellKey);

        if (!trend || !trend.labels.length || !trend.hasData) {
            clearDensityTrendPreview();
            if (state.densityTrendModalOpen) {
                clearDensityTrendModal();
            }
            return;
        }

        state.densityTrendPreviewChart.setOption(buildDensityPreviewOption(trend), true);

        if (state.densityTrendModalOpen) {
            renderDensityTrendModal(trend);
        }
    } catch (error) {
        console.error("加载网格趋势失败:", error);
        clearDensityTrendPreview();
        if (state.densityTrendModalOpen) {
            clearDensityTrendModal();
        }
    }
}

export {
    clearDensityChunkCache,
    hideDensityTooltip,
    showDensityTooltip,
    clearDensityQueryState,
    clearDensityCacheOnly,
    resetDensityState,
    getDensityGridModel,
    buildDensityChunkMaps,
    getDensityCellBoundsFromGrid,
    getDensityChunkCells,
    getDensityChunkCacheKey,
    getDensityChunkRange,
    getDensityVisibleChunks,
    evictDensityChunkCacheIfNeeded,
    renderDensityChunkToCanvas,
    getDensityChunkRender,
    parseDensityCellKey,
    getDensityCellByKey,
    getDensityCellBounds,
    drawDensityGridBase,
    drawDensityHeatCells,
    getVisibleDensityGridRange,
    drawDensityBucket,
    getDensityRatio,
    mixColor,
    getHeatColor,
    getCurrentBucket,
    trimDensityBucketCache,
    loadCellTrend,
    prefetchDensityBuckets,
    loadDensityBucketInBackground,
    getBucketLabel,
    getBucketShortLabel,
    buildDensityCellMaps,
    updateDensityTimeLabel,
    setDensityBucketIndex,
    ensureDensityBucketLoaded,
    renderSelectedCellTrend
};
