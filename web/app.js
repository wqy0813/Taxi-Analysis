const state = {
    meta: null,
    map: null,
    region: null,
    regionPolygon: null,
    selectingRegion: false,
    selectionStartPixel: null,
    selectionEndPixel: null,
    densityResult: null,
    currentBucketIndex: 0,
    densityCanvas: null,
    densityCtx: null,
    densityCellMaps: [],
    densityPlayTimer: null,
    densityHoverCell: null,
    densitySelectedCellKey: null,
    densityTrendCanvas: null,
    densityTrendCtx: null,
    densityTooltip: null,
    trajectoryOverlays: [],
    allTaxiPointCollection: null,
    allTaxiMode: false,
    allTaxiRequestSeq: 0,
    allTaxiRefreshTimer: null,
    currentAllTaxiPointCount: 0,
    currentAllTaxiRenderMode: "cluster",
    selectionLayer: null,
    activeDockPanel: null,
    openDockPanel: null
};

const API_BASE = location.protocol === "file:" ? "http://127.0.0.1:8080" : "";

function qs(id) {
    return document.getElementById(id);
}

function setText(id, text) {
    const element = qs(id);
    if (element) {
        element.textContent = text;
    }
}

function setInfoPanel(id, lines, empty = false) {
    const element = qs(id);
    if (!element) {
        return;
    }
    element.classList.toggle("empty", empty);
    element.textContent = lines.join("\n");
}

function escapeHtml(value) {
    return String(value)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#39;");
}

function renderInfoPanel(id, rows, emptyText = "等待查询") {
    const element = qs(id);
    if (!element) {
        return;
    }

    if (!rows || rows.length === 0) {
        element.classList.add("empty");
        element.innerHTML = `<div class="info-empty">${escapeHtml(emptyText)}</div>`;
        return;
    }

    element.classList.remove("empty");
    element.innerHTML = `<div class="info-list">${rows.map(([key, value]) => `
        <div class="info-row">
            <span class="info-key">${escapeHtml(key)}</span>
            <span class="info-value">${escapeHtml(value)}</span>
        </div>
    `).join("")}</div>`;
}

function formatCount(value) {
    return Number(value || 0).toLocaleString("zh-CN");
}

function formatDateTime(epochSeconds) {
    const date = new Date(Number(epochSeconds) * 1000);
    const pad = (value) => String(value).padStart(2, "0");
    return `${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}`;
}

async function requestJson(url, options = {}) {
    const response = await fetch(`${API_BASE}${url}`, {
        headers: { "Content-Type": "application/json" },
        ...options
    });
    const payload = await response.json();
    if (!payload.success) {
        throw new Error(payload.error?.message || "请求失败");
    }
    return payload.data;
}

function parseDateTimeInput(value, label) {
    if (!value) {
        throw new Error(`${label} 不能为空`);
    }
    const timestamp = Date.parse(value);
    if (Number.isNaN(timestamp)) {
        throw new Error(`${label} 无效`);
    }
    return Math.floor(timestamp / 1000);
}

function ensureRegion() {
    if (!state.region) {
        throw new Error("请先框选区域");
    }
}

function ensureTimeRange(startValue, endValue) {
    const startTime = parseDateTimeInput(startValue, "开始时间");
    const endTime = parseDateTimeInput(endValue, "结束时间");
    if (startTime > endTime) {
        throw new Error("开始时间不能晚于结束时间");
    }
    return { startTime, endTime };
}

function updateMetaStatus() {
    if (!state.meta) {
        return;
    }
    setText("meta-total-points", formatCount(state.meta.totalPoints));
    setText("server-status", "在线");
}

function updateModeStatus(text) {
    setText("stage-status", text);
}

function updateRegionStatus() {
    const badge = qs("region-state-badge");

    if (!state.region) {
        if (badge) {
            badge.textContent = "未选";
            badge.className = "badge muted";
        }
        setText("status-region", "未选");
        renderInfoPanel("region-info", [], "未框选");
        return;
    }

    if (badge) {
        badge.textContent = "已锁定";
        badge.className = "badge";
    }

    const summary = `${state.region.minLon.toFixed(4)}, ${state.region.minLat.toFixed(4)} 路 ${state.region.maxLon.toFixed(4)}, ${state.region.maxLat.toFixed(4)}`;
    setText("status-region", summary);
    renderInfoPanel("region-info", [
        ["经度", `${state.region.minLon.toFixed(6)} ~ ${state.region.maxLon.toFixed(6)}`],
        ["纬度", `${state.region.minLat.toFixed(6)} ~ ${state.region.maxLat.toFixed(6)}`]
    ]);
}

function setDockPanelState(name, open) {
    hideSelectionBox();
    if (state.selectingRegion) {
        cancelRegionSelection();
    }

    if (name !== "query") {
        clearRegionOverlay();
    }

    document.querySelectorAll(".tool-btn[data-panel]").forEach((button) => {
        const isActive = name !== null && button.dataset.panel === name;
        const isOpen = open !== null && button.dataset.panel === open;
        button.classList.toggle("active", isActive);
        button.setAttribute("aria-expanded", String(isOpen));
    });

    document.querySelectorAll(".tool-panel[data-panel]").forEach((panel) => {
        panel.classList.toggle("is-open", open !== null && panel.dataset.panel === open);
    });

    state.activeDockPanel = name;
    state.openDockPanel = open;
}

function activateDockPanel(name) {
    if (state.activeDockPanel === name) {
        const nextOpen = state.openDockPanel === name ? null : name;
        setDockPanelState(name, nextOpen);
        return;
    }

    setDockPanelState(name, name);
}

function clearDockSelection() {
    hideSelectionBox();
    if (state.selectingRegion) {
        cancelRegionSelection();
    }
    stopAllTaxiMode(true);
    state.region = null;
    clearRegionOverlay();
    resetDensityState();
    updateRegionStatus();
    updateModeStatus("地图");
    renderInfoPanel("trajectory-info", [], "等待查询");
    renderInfoPanel("region-query-info", [], "等待查询");
    renderInfoPanel("density-info", [], "等待查询");
    state.activeDockPanel = null;
    state.openDockPanel = null;
    document.querySelectorAll(".tool-btn[data-panel]").forEach((button) => {
        button.classList.remove("active");
        button.setAttribute("aria-expanded", "false");
    });
    document.querySelectorAll(".tool-panel[data-panel]").forEach((panel) => {
        panel.classList.remove("is-open");
    });
}

function loadBaiduMapScript(ak) {
    return new Promise((resolve, reject) => {
        if (window.BMap) {
            resolve();
            return;
        }
        if (!ak) {
            reject(new Error("缺少地图 AK"));
            return;
        }

        const callbackName = "__baiduMapReady";
        window[callbackName] = () => {
            delete window[callbackName];
            resolve();
        };

        const script = document.createElement("script");
        script.src = `https://api.map.baidu.com/api?v=3.0&ak=${encodeURIComponent(ak)}&callback=${callbackName}`;
        script.async = true;
        script.onerror = () => reject(new Error("地图加载失败"));
        document.head.appendChild(script);
    });
}

function initMap() {
    const centerPoint = new BMap.Point(state.meta.centerLon, state.meta.centerLat);
    state.map = new BMap.Map("map", { enableMapClick: false });
    state.map.centerAndZoom(centerPoint, state.meta.initialZoom);
    state.map.enableScrollWheelZoom(true);
    state.map.enableDoubleClickZoom(true);
    state.map.enableKeyboard();
    state.map.enableDragging();

    if (typeof state.map.setMinZoom === "function") {
        state.map.setMinZoom(state.meta.minZoom || 8);
    }
    if (typeof state.map.setMaxZoom === "function") {
        state.map.setMaxZoom(state.meta.maxZoom || 18);
    }

    const refresh = () => {
        drawDensityBucket();
        scheduleAllTaxiRefresh();
    };

    state.map.addEventListener("zoomend", refresh);
    state.map.addEventListener("moveend", refresh);
    window.addEventListener("resize", () => {
        syncDensityCanvasSize();
        syncDensityTrendCanvasSize();
        drawDensityBucket();
        renderSelectedCellTrend();
    });
}

function clearTrajectoryOverlays() {
    if (!state.map) {
        return;
    }

    for (const overlay of state.trajectoryOverlays) {
        state.map.removeOverlay(overlay);
    }
    state.trajectoryOverlays = [];

    if (state.allTaxiPointCollection) {
        state.map.removeOverlay(state.allTaxiPointCollection);
        state.allTaxiPointCollection = null;
    }
}

function addTrajectoryOverlay(overlay) {
    state.map.addOverlay(overlay);
    state.trajectoryOverlays.push(overlay);
}

function clearRegionOverlay() {
    if (state.regionPolygon) {
        state.map.removeOverlay(state.regionPolygon);
        state.regionPolygon = null;
    }
}

function clearDensityCanvas() {
    if (!state.densityCtx) {
        return;
    }
    const rect = state.densityCanvas.getBoundingClientRect();
    state.densityCtx.clearRect(0, 0, rect.width, rect.height);
}

function resetDensityState() {
    if (state.densityPlayTimer) {
        clearInterval(state.densityPlayTimer);
        state.densityPlayTimer = null;
    }
    state.densityResult = null;
    state.currentBucketIndex = 0;
    state.densityCellMaps = [];
    state.densityHoverCell = null;
    state.densitySelectedCellKey = null;
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
    clearDensityTrendCanvas();
    clearDensityCanvas();
}

function createRegionPolygon(region) {
    const points = [
        new BMap.Point(region.minLon, region.maxLat),
        new BMap.Point(region.maxLon, region.maxLat),
        new BMap.Point(region.maxLon, region.minLat),
        new BMap.Point(region.minLon, region.minLat)
    ];

    return new BMap.Polygon(points, {
        strokeColor: "#35b9b1",
        strokeWeight: 2,
        strokeOpacity: 0.9,
        fillColor: "#72b9f4",
        fillOpacity: 0.12
    });
}

function renderRegion(region) {
    clearRegionOverlay();
    state.regionPolygon = createRegionPolygon(region);
    state.map.addOverlay(state.regionPolygon);
}

function ensureDensityCanvas() {
    state.densityCanvas = qs("density-canvas");
    state.densityCtx = state.densityCanvas.getContext("2d");
    state.densityTrendCanvas = qs("density-trend-canvas");
    state.densityTrendCtx = state.densityTrendCanvas.getContext("2d");
    state.densityTooltip = qs("density-tooltip");
    syncDensityTrendCanvasSize();
    syncDensityCanvasSize();
}

function syncDensityCanvasSize() {
    if (!state.densityCanvas || !state.densityCtx) {
        return;
    }

    const rect = state.densityCanvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    state.densityCanvas.width = Math.max(1, Math.round(rect.width * dpr));
    state.densityCanvas.height = Math.max(1, Math.round(rect.height * dpr));
    state.densityCtx.setTransform(1, 0, 0, 1, 0, 0);
    state.densityCtx.scale(dpr, dpr);
}

function syncDensityTrendCanvasSize() {
    if (!state.densityTrendCanvas || !state.densityTrendCtx) {
        return;
    }
    const rect = state.densityTrendCanvas.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    state.densityTrendCanvas.width = Math.max(1, Math.round(rect.width * dpr));
    state.densityTrendCanvas.height = Math.max(1, Math.round(rect.height * dpr));
    state.densityTrendCtx.setTransform(1, 0, 0, 1, 0, 0);
    state.densityTrendCtx.scale(dpr, dpr);
}

function pointToPixel(lon, lat) {
    return state.map.pointToPixel(new BMap.Point(lon, lat));
}

// 将地图点统一换算为当前 density canvas 所在容器坐标。
// 说明：
// - BMap 在不同 API 下可能返回不同参考系（容器像素 / 覆盖物像素）。
// - 这里以“容器像素”为目标坐标，并用中心点做一次偏移校准，
//   保证网格绘制、点击高亮、tooltip 定位使用同一坐标系。
function pointToCanvasPixel(lon, lat) {
    const point = new BMap.Point(lon, lat);
    const pixel = state.map.pointToPixel(point);

    if (typeof state.map.pointToOverlayPixel !== "function") {
        return pixel;
    }

    const overlayPixel = state.map.pointToOverlayPixel(point);
    const center = state.map.getCenter();
    const centerPixel = state.map.pointToPixel(center);
    const centerOverlayPixel = state.map.pointToOverlayPixel(center);
    const offsetX = centerOverlayPixel.x - centerPixel.x;
    const offsetY = centerOverlayPixel.y - centerPixel.y;

    return {
        x: overlayPixel.x - offsetX,
        y: overlayPixel.y - offsetY
    };
}

function pixelToPoint(x, y) {
    return state.map.pixelToPoint(new BMap.Pixel(x, y));
}

function isCellVisibleInCanvas(left, top, width, height, canvasWidth, canvasHeight) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (left >= canvasWidth || top >= canvasHeight) {
        return false;
    }
    if (left + width <= 0 || top + height <= 0) {
        return false;
    }
    return true;
}

// 根据网格索引反推网格经纬度边界。
// 后端已不再逐单元返回 min/max 经纬度，减少了返回体积；
// 前端统一用该函数在绘制/高亮时按需计算。
function getDensityCellBounds(cell) {
    if (!state.densityResult || !cell) {
        return null;
    }

    // 优先使用后端显式返回的网格边界，避免前端重算带来的累计误差。
    if (Number.isFinite(Number(cell.minLon)) &&
        Number.isFinite(Number(cell.minLat)) &&
        Number.isFinite(Number(cell.maxLon)) &&
        Number.isFinite(Number(cell.maxLat))) {
        return {
            minLon: Number(cell.minLon),
            minLat: Number(cell.minLat),
            maxLon: Number(cell.maxLon),
            maxLat: Number(cell.maxLat)
        };
    }

    const minLon = Number(state.densityResult.minLon);
    const minLat = Number(state.densityResult.minLat);
    const maxLon = Number(state.densityResult.maxLon);
    const maxLat = Number(state.densityResult.maxLat);
    const lonStep = Number(state.densityResult.lonStep);
    const latStep = Number(state.densityResult.latStep);

    if (!Number.isFinite(minLon) || !Number.isFinite(minLat) ||
        !Number.isFinite(maxLon) || !Number.isFinite(maxLat) ||
        !Number.isFinite(lonStep) || !Number.isFinite(latStep) ||
        lonStep <= 0 || latStep <= 0) {
        return null;
    }

    const cellMinLon = minLon + Number(cell.gx || 0) * lonStep;
    const cellMinLat = minLat + Number(cell.gy || 0) * latStep;
    return {
        minLon: cellMinLon,
        minLat: cellMinLat,
        maxLon: Math.min(cellMinLon + lonStep, maxLon),
        maxLat: Math.min(cellMinLat + latStep, maxLat)
    };
}

function drawDensityBucket() {
    clearDensityCanvas();
    if (!state.densityResult || !state.densityResult.buckets || !state.densityResult.buckets.length || !state.map) {
        return;
    }

    const bucket = state.densityResult.buckets[state.currentBucketIndex];
    if (!bucket) {
        return;
    }

    const canvasRect = state.densityCanvas.getBoundingClientRect();
    const canvasWidth = canvasRect.width;
    const canvasHeight = canvasRect.height;

    // 先绘制填充，再在可辨识网格上补边框，避免密集场景下边框噪声过重。
    for (const cell of bucket.cells) {
        const bounds = getDensityCellBounds(cell);
        if (!bounds) {
            continue;
        }
        const topLeft = pointToCanvasPixel(bounds.minLon, bounds.maxLat);
        const bottomRight = pointToCanvasPixel(bounds.maxLon, bounds.minLat);
        const left = Math.min(topLeft.x, bottomRight.x);
        const top = Math.min(topLeft.y, bottomRight.y);
        const width = Math.abs(bottomRight.x - topLeft.x);
        const height = Math.abs(bottomRight.y - topLeft.y);

        if (!isCellVisibleInCanvas(left, top, width, height, canvasWidth, canvasHeight)) {
            continue;
        }

        const ratio = getDensityRatio(cell);
        const fillColor = getHeatColor(ratio);
        state.densityCtx.fillStyle = fillColor;
        state.densityCtx.fillRect(left, top, width, height);
        // 网格足够大时才绘制边框，避免缩放较小时出现“灰网”噪点。
        if (width >= 6 && height >= 6) {
            state.densityCtx.strokeStyle = ratio > 0.72 ? "rgba(214, 71, 64, 0.55)" : "rgba(105, 142, 166, 0.26)";
            state.densityCtx.lineWidth = 1;
            state.densityCtx.strokeRect(left + 0.5, top + 0.5, Math.max(0, width - 1), Math.max(0, height - 1));
        }
    }

    if (state.densitySelectedCellKey) {
        const selected = state.densityCellMaps[state.currentBucketIndex]?.get(state.densitySelectedCellKey);
        if (selected) {
            const bounds = getDensityCellBounds(selected);
            if (bounds) {
                const topLeft = pointToCanvasPixel(bounds.minLon, bounds.maxLat);
                const bottomRight = pointToCanvasPixel(bounds.maxLon, bounds.minLat);
                const left = Math.min(topLeft.x, bottomRight.x);
                const top = Math.min(topLeft.y, bottomRight.y);
                const width = Math.abs(bottomRight.x - topLeft.x);
                const height = Math.abs(bottomRight.y - topLeft.y);
                state.densityCtx.strokeStyle = "rgba(19, 52, 71, 0.92)";
                state.densityCtx.lineWidth = 2;
                state.densityCtx.strokeRect(left + 0.5, top + 0.5, Math.max(0, width - 1), Math.max(0, height - 1));
            }
        }
    }
}

function getDensityRatio(cell) {
    if (!state.densityResult) {
        return 0;
    }
    const maxDensity = Number(state.densityResult.maxVehicleDensity || 0);
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

function getHeatColor(ratio) {
    // 更克制的三段色带：低密度蓝青，中密度黄橙，高密度红。
    const low = [164, 222, 240];
    const mid = [243, 186, 73];
    const high = [223, 79, 73];
    let rgb;
    if (ratio <= 0.5) {
        rgb = mixColor(low, mid, ratio / 0.5);
    } else {
        rgb = mixColor(mid, high, (ratio - 0.5) / 0.5);
    }
    const alpha = 0.16 + ratio * 0.6;
    return `rgba(${rgb[0]}, ${rgb[1]}, ${rgb[2]}, ${alpha.toFixed(3)})`;
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

function getCurrentBucket() {
    return state.densityResult?.buckets?.[state.currentBucketIndex] || null;
}

function getBucketLabel(bucket) {
    if (!bucket) {
        return "-";
    }
    return `${formatDateTime(bucket.startTime)} - ${formatDateTime(bucket.endTime)}`;
}

function clearDensityTrendCanvas() {
    if (!state.densityTrendCanvas || !state.densityTrendCtx) {
        return;
    }
    const rect = state.densityTrendCanvas.getBoundingClientRect();
    state.densityTrendCtx.clearRect(0, 0, rect.width, rect.height);
}

function renderSelectedCellTrend() {
    clearDensityTrendCanvas();
    if (!state.densityResult || !state.densitySelectedCellKey || !state.densityTrendCtx || !state.densityTrendCanvas) {
        return;
    }

    const values = state.densityResult.buckets.map((_, index) => {
        const cell = state.densityCellMaps[index]?.get(state.densitySelectedCellKey);
        return Number(cell?.vehicleDensity || 0);
    });

    const maxValue = Math.max(0, ...values);
    const rect = state.densityTrendCanvas.getBoundingClientRect();
    const width = rect.width;
    const height = rect.height;
    const padding = 18;

    state.densityTrendCtx.strokeStyle = "rgba(136, 163, 183, 0.35)";
    state.densityTrendCtx.lineWidth = 1;
    state.densityTrendCtx.beginPath();
    state.densityTrendCtx.moveTo(padding, height - padding);
    state.densityTrendCtx.lineTo(width - padding, height - padding);
    state.densityTrendCtx.moveTo(padding, padding);
    state.densityTrendCtx.lineTo(padding, height - padding);
    state.densityTrendCtx.stroke();

    if (values.length <= 1 || maxValue <= 0) {
        return;
    }

    state.densityTrendCtx.strokeStyle = "rgba(47, 185, 177, 0.9)";
    state.densityTrendCtx.lineWidth = 2;
    state.densityTrendCtx.beginPath();

    for (let i = 0; i < values.length; i += 1) {
        const x = padding + (i / (values.length - 1)) * (width - padding * 2);
        const y = height - padding - (values[i] / maxValue) * (height - padding * 2);
        if (i === 0) {
            state.densityTrendCtx.moveTo(x, y);
        } else {
            state.densityTrendCtx.lineTo(x, y);
        }
    }
    state.densityTrendCtx.stroke();
}

function updateDensityTimeLabel() {
    const bucket = getCurrentBucket();
    setText("density-time-label", getBucketLabel(bucket));
}

function setDensityBucketIndex(index) {
    if (!state.densityResult || !state.densityResult.buckets?.length) {
        return;
    }
    const maxIndex = state.densityResult.buckets.length - 1;
    state.currentBucketIndex = Math.max(0, Math.min(maxIndex, index));

    const bucketSelect = qs("density-bucket");
    if (bucketSelect) {
        bucketSelect.value = String(state.currentBucketIndex);
    }
    const timeline = qs("density-timeline");
    if (timeline) {
        timeline.value = String(state.currentBucketIndex);
    }

    updateDensityTimeLabel();
    drawDensityBucket();
    if (state.densitySelectedCellKey) {
        const selected = state.densityCellMaps[state.currentBucketIndex]?.get(state.densitySelectedCellKey);
        updateTrendSummary(state.densitySelectedCellKey, selected || null);
        renderSelectedCellTrend();
    }
}

function normalizePixels(startPixel, endPixel) {
    return {
        left: Math.min(startPixel.x, endPixel.x),
        top: Math.min(startPixel.y, endPixel.y),
        right: Math.max(startPixel.x, endPixel.x),
        bottom: Math.max(startPixel.y, endPixel.y)
    };
}

function updateSelectionBox(startPixel, endPixel) {
    const selectionBox = qs("selection-box");
    const box = normalizePixels(startPixel, endPixel);
    selectionBox.style.display = "block";
    selectionBox.style.left = `${box.left}px`;
    selectionBox.style.top = `${box.top}px`;
    selectionBox.style.width = `${Math.max(1, box.right - box.left)}px`;
    selectionBox.style.height = `${Math.max(1, box.bottom - box.top)}px`;
}

function hideSelectionBox() {
    const selectionBox = qs("selection-box");
    selectionBox.style.display = "none";
    selectionBox.style.width = "0";
    selectionBox.style.height = "0";
}

function stopAllTaxiMode(clearVisuals = true) {
    state.allTaxiMode = false;
    state.allTaxiRequestSeq += 1;
    if (state.allTaxiRefreshTimer) {
        clearTimeout(state.allTaxiRefreshTimer);
        state.allTaxiRefreshTimer = null;
    }
    if (clearVisuals) {
        clearTrajectoryOverlays();
    }
}

function startRegionSelection() {
    stopAllTaxiMode();
    state.selectingRegion = true;
    state.selectionStartPixel = null;
    state.selectionEndPixel = null;
    updateModeStatus("选区");
    state.map.disableDragging();
    if (state.selectionLayer) {
        state.selectionLayer.classList.add("active");
    }
}

function cancelRegionSelection() {
    state.selectingRegion = false;
    state.selectionStartPixel = null;
    state.selectionEndPixel = null;
    state.map.enableDragging();
    hideSelectionBox();
    updateModeStatus("地图");
    if (state.selectionLayer) {
        state.selectionLayer.classList.remove("active");
    }
}

function installRegionSelection() {
    state.selectionLayer = qs("selection-layer");
    const layer = state.selectionLayer;

    layer.addEventListener("pointerdown", (event) => {
        if (!state.selectingRegion) {
            return;
        }

        event.preventDefault();
        try {
            layer.setPointerCapture(event.pointerId);
        } catch (_) {
        }

        const rect = layer.getBoundingClientRect();
        state.selectionStartPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        state.selectionEndPixel = state.selectionStartPixel;
        updateSelectionBox(state.selectionStartPixel, state.selectionEndPixel);
    });

    layer.addEventListener("pointermove", (event) => {
        if (!state.selectingRegion || !state.selectionStartPixel) {
            return;
        }

        const rect = layer.getBoundingClientRect();
        state.selectionEndPixel = {
            x: Math.max(0, Math.min(rect.width, event.clientX - rect.left)),
            y: Math.max(0, Math.min(rect.height, event.clientY - rect.top))
        };
        updateSelectionBox(state.selectionStartPixel, state.selectionEndPixel);
    });

    layer.addEventListener("pointerup", (event) => {
        if (!state.selectingRegion || !state.selectionStartPixel || !state.selectionEndPixel) {
            return;
        }

        try {
            if (layer.hasPointerCapture(event.pointerId)) {
                layer.releasePointerCapture(event.pointerId);
            }
        } catch (_) {
        }

        const box = normalizePixels(state.selectionStartPixel, state.selectionEndPixel);
        hideSelectionBox();
        cancelRegionSelection();

        if (box.right - box.left < 6 || box.bottom - box.top < 6) {
            return;
        }

        const leftTop = pixelToPoint(box.left, box.top);
        const rightBottom = pixelToPoint(box.right, box.bottom);
        state.region = {
            minLon: Math.min(leftTop.lng, rightBottom.lng),
            minLat: Math.min(rightBottom.lat, leftTop.lat),
            maxLon: Math.max(leftTop.lng, rightBottom.lng),
            maxLat: Math.max(rightBottom.lat, leftTop.lat)
        };

        renderRegion(state.region);
        resetDensityState();
        updateRegionStatus();
    });
}

function currentMapBounds() {
    const bounds = state.map.getBounds();
    const sw = bounds.getSouthWest();
    const ne = bounds.getNorthEast();
    return {
        minLon: sw.lng,
        minLat: sw.lat,
        maxLon: ne.lng,
        maxLat: ne.lat
    };
}

function createDotIcon() {
    const svg = `
        <svg xmlns="http://www.w3.org/2000/svg" width="12" height="12" viewBox="0 0 12 12">
            <circle cx="6" cy="6" r="5" fill="#35b9b1" stroke="#ffffff" stroke-width="1.8"/>
        </svg>
    `;
    return new BMap.Icon(`data:image/svg+xml;charset=UTF-8,${encodeURIComponent(svg)}`, new BMap.Size(12, 12), {
        anchor: new BMap.Size(6, 6)
    });
}

function createPinIcon() {
    const svg = `
        <svg xmlns="http://www.w3.org/2000/svg" width="28" height="40" viewBox="0 0 28 40">
            <path d="M14 39s10-12 10-22A10 10 0 0 0 4 17C4 27 14 39 14 39Z" fill="#35b9b1" stroke="#ffffff" stroke-width="2"/>
            <circle cx="14" cy="17" r="4.5" fill="#ffffff"/>
        </svg>
    `;
    return new BMap.Icon(`data:image/svg+xml;charset=UTF-8,${encodeURIComponent(svg)}`, new BMap.Size(28, 40), {
        anchor: new BMap.Size(14, 40)
    });
}

function createBoxLabel(point, text, offsetX = -12, offsetY = -12) {
    const label = new BMap.Label(String(text), {
        position: point,
        offset: new BMap.Size(offsetX, offsetY)
    });
    label.setStyle({
        color: "#12313f",
        backgroundColor: "rgba(255, 255, 255, 0.92)",
        border: "1px solid rgba(53, 185, 177, 0.42)",
        borderRadius: "999px",
        minWidth: "18px",
        height: "18px",
        padding: "0 4px",
        fontSize: "11px",
        fontWeight: "700",
        lineHeight: "18px",
        textAlign: "center",
        boxShadow: "0 8px 16px rgba(26, 55, 84, 0.1)",
        whiteSpace: "nowrap"
    });
    return label;
}

function addPinCluster(item) {
    const point = new BMap.Point(item.lng, item.lat);
    const marker = new BMap.Marker(point, { icon: createPinIcon() });
    addTrajectoryOverlay(marker);
    marker.addEventListener("click", () => {
        if (item.minLon !== undefined && item.maxLon !== undefined) {
            const p1 = new BMap.Point(item.minLon, item.minLat);
            const p2 = new BMap.Point(item.maxLon, item.maxLat);
            state.map.setViewport([p1, p2]);
        }
    });
}

function addBoxCluster(item) {
    const center = new BMap.Point(item.lng, item.lat);
    const label = createBoxLabel(center, item.count, -10, -10);
    addTrajectoryOverlay(label);
    label.addEventListener("click", () => {
        const p1 = new BMap.Point(item.minLon, item.minLat);
        const p2 = new BMap.Point(item.maxLon, item.maxLat);
        state.map.setViewport([p1, p2]);
    });
}

function addDotPoint(item) {
    const point = new BMap.Point(item.lng, item.lat);
    const marker = new BMap.Marker(point, { icon: createDotIcon() });
    addTrajectoryOverlay(marker);
}

function renderAllTaxiRawPoints(points) {
    clearTrajectoryOverlays();
    if (!points || points.length === 0) {
        renderInfoPanel("trajectory-info", [], "当前视野内没有车辆点");
        return;
    }

    const zoom = state.map.getZoom();
    const rawPoints = points.map((point) => new BMap.Point(point.lon, point.lat));

    if (window.BMap.PointCollection || window.PointCollection) {
        const pointCollectionCtor = window.BMap.PointCollection || window.PointCollection;
        const options = { color: "#2dd4bf" };
        if (typeof window.BMAP_POINT_SIZE_SMALL !== "undefined") {
            options.size = window.BMAP_POINT_SIZE_SMALL;
        }
        if (typeof window.BMAP_POINT_SHAPE_CIRCLE !== "undefined") {
            options.shape = window.BMAP_POINT_SHAPE_CIRCLE;
        }
        const pointCollection = new pointCollectionCtor(rawPoints, options);
        state.map.addOverlay(pointCollection);
        state.allTaxiPointCollection = pointCollection;
    } else {
        for (const point of rawPoints) {
            const marker = new BMap.Marker(point, { icon: createDotIcon() });
            addTrajectoryOverlay(marker);
        }
    }

    state.currentAllTaxiRenderMode = "raw";
    renderInfoPanel("trajectory-info", [
        ["模式", "全部车辆"],
        ["GPS 采样点", formatCount(state.currentAllTaxiPointCount)],
        ["缩放", String(zoom)]
    ]);
}

function renderAllTaxiClusters(items) {
    clearTrajectoryOverlays();
    if (!items || items.length === 0) {
        renderInfoPanel("trajectory-info", [], "当前视野内没有车辆点");
        return;
    }

    const zoom = state.map.getZoom();
    const mode = zoom <= 12 ? "pin" : "box";

    for (const item of items) {
        const isCluster = !!item.isCluster || (item.count && item.count > 1);
        if (mode === "pin") {
            addPinCluster(item);
        } else if (isCluster) {
            addBoxCluster(item);
        } else {
            addDotPoint(item);
        }
    }

    state.currentAllTaxiRenderMode = mode;
    renderInfoPanel("trajectory-info", [
        ["模式", "全部车辆"],
        ["GPS 采样点", formatCount(state.currentAllTaxiPointCount)],
        ["聚合项", formatCount(items.length)],
        ["缩放", String(zoom)]
    ]);
}

function renderAllTaxiData(data) {
    if ((data.mode || "").toLowerCase() === "raw") {
        renderAllTaxiRawPoints(data.points || []);
        return;
    }
    renderAllTaxiClusters(data.points || []);
}

function scheduleAllTaxiRefresh() {
    if (!state.allTaxiMode) {
        return;
    }

    if (state.allTaxiRefreshTimer) {
        clearTimeout(state.allTaxiRefreshTimer);
    }

    state.allTaxiRefreshTimer = setTimeout(() => {
        state.allTaxiRefreshTimer = null;
        refreshAllTaxiView();
    }, 180);
}

async function refreshAllTaxiView() {
    if (!state.allTaxiMode || !state.map) {
        return;
    }

    const requestSeq = ++state.allTaxiRequestSeq;
    const bounds = currentMapBounds();
    const zoom = state.map.getZoom();

    renderInfoPanel("trajectory-info", [
        ["模式", "全部车辆"],
        ["状态", "加载中"],
        ["缩放", String(zoom)]
    ]);

    const data = await requestJson("/api/trajectory", {
        method: "POST",
        body: JSON.stringify({
            taxiId: 0,
            zoom,
            ...bounds
        })
    });

    if (requestSeq !== state.allTaxiRequestSeq) {
        return;
    }

    state.currentAllTaxiPointCount = data.pointCount || 0;
    renderAllTaxiData(data);
}

async function runTrajectoryQuery() {
    const taxiId = Number(qs("trajectory-id").value);
    if (!Number.isInteger(taxiId) || taxiId < 0) {
        throw new Error("车辆 ID 必须是非负整数");
    }

    if (taxiId === 0) {
        state.allTaxiMode = true;
        state.currentAllTaxiPointCount = 0;
        resetDensityState();
        await refreshAllTaxiView();
        return;
    }

    stopAllTaxiMode(true);
    clearDensityCanvas();

    const data = await requestJson("/api/trajectory", {
        method: "POST",
        body: JSON.stringify({ taxiId })
    });

    clearTrajectoryOverlays();
    const path = data.points.map((point) => new BMap.Point(point.lon, point.lat));
    if (path.length > 0) {
        const polyline = new BMap.Polyline(path, {
            strokeColor: "#2dd4bf",
            strokeWeight: 3,
            strokeOpacity: 0.88
        });
        addTrajectoryOverlay(polyline);
        state.map.setViewport(path);
    }

    renderInfoPanel("trajectory-info", [
        ["车辆 ID", String(data.taxiId)],
        ["轨迹点", formatCount(data.pointCount)]
    ]);
}

async function runRegionQuery() {
    stopAllTaxiMode(true);
    ensureRegion();
    const { startTime, endTime } = ensureTimeRange(qs("region-start").value, qs("region-end").value);

    const data = await requestJson("/api/region-search", {
        method: "POST",
        body: JSON.stringify({
            ...state.region,
            startTime,
            endTime
        })
    });

    renderInfoPanel("region-query-info", [
        ["覆盖车辆", formatCount(data.vehicleCount)],
        ["采样点", formatCount(data.pointCount)],
        ["用时", `${Number(data.elapsedSeconds).toFixed(3)} s`]
    ]);
}

function buildDensityCellMaps() {
    state.densityCellMaps = (state.densityResult?.buckets || []).map((bucket) => {
        const map = new Map();
        for (const cell of bucket.cells || []) {
            map.set(`${cell.gx}:${cell.gy}`, cell);
        }
        return map;
    });
}

function tryGetCellByPoint(point) {
    if (!state.densityResult || !point) {
        return null;
    }

    const lonStep = Number(state.densityResult.lonStep || 0);
    const latStep = Number(state.densityResult.latStep || 0);
    if (lonStep <= 0 || latStep <= 0) {
        return null;
    }

    const minLon = Number(state.densityResult.minLon);
    const minLat = Number(state.densityResult.minLat);
    const maxLon = Number(state.densityResult.maxLon);
    const maxLat = Number(state.densityResult.maxLat);
    if (!Number.isFinite(minLon) || !Number.isFinite(minLat) || !Number.isFinite(maxLon) || !Number.isFinite(maxLat)) {
        return null;
    }
    if (point.lng < minLon || point.lng > maxLon || point.lat < minLat || point.lat > maxLat) {
        return null;
    }

    const gx = Math.max(0, Math.min(
        Number(state.densityResult.columnCount || 1) - 1,
        Math.floor((point.lng - minLon) / lonStep)
    ));
    const gy = Math.max(0, Math.min(
        Number(state.densityResult.rowCount || 1) - 1,
        Math.floor((point.lat - minLat) / latStep)
    ));

    const key = `${gx}:${gy}`;
    const cell = state.densityCellMaps[state.currentBucketIndex]?.get(key);
    return cell ? { key, cell } : null;
}

function updateTrendSummary(key, cell) {
    const bucket = getCurrentBucket();
    if (!key || !cell || !bucket) {
        renderInfoPanel("density-trend-summary", [], "点击网格查看趋势");
        return;
    }

    renderInfoPanel("density-trend-summary", [
        ["网格", key],
        ["时间段", getBucketLabel(bucket)],
        ["车辆数", formatCount(cell.vehicleCount)],
        ["密度", Number(cell.vehicleDensity || 0).toFixed(2)]
    ]);
}

function installDensityMapInteractions() {
    state.map.addEventListener("mousemove", (event) => {
        if (!state.densityResult) {
            hideDensityTooltip();
            return;
        }

        const hit = tryGetCellByPoint(event.point);
        if (!hit) {
            state.densityHoverCell = null;
            hideDensityTooltip();
            return;
        }

        state.densityHoverCell = hit.key;
        const bucket = getCurrentBucket();
        const html = [
            `<div><strong>网格:</strong> ${escapeHtml(hit.key)}</div>`,
            `<div><strong>时间段:</strong> ${escapeHtml(getBucketLabel(bucket))}</div>`,
            `<div><strong>车辆数:</strong> ${escapeHtml(formatCount(hit.cell.vehicleCount))}</div>`,
            `<div><strong>密度:</strong> ${escapeHtml(Number(hit.cell.vehicleDensity || 0).toFixed(2))}</div>`
        ].join("");
        const pixel = pointToCanvasPixel(event.point.lng, event.point.lat);
        showDensityTooltip(html, pixel.x, pixel.y);
    });

    state.map.addEventListener("mouseout", () => {
        hideDensityTooltip();
    });

    state.map.addEventListener("click", (event) => {
        if (!state.densityResult) {
            return;
        }
        const hit = tryGetCellByPoint(event.point);
        if (!hit) {
            state.densitySelectedCellKey = null;
            updateTrendSummary(null, null);
            clearDensityTrendCanvas();
            drawDensityBucket();
            return;
        }

        state.densitySelectedCellKey = hit.key;
        updateTrendSummary(hit.key, hit.cell);
        renderSelectedCellTrend();
        drawDensityBucket();
    });
}

function stopDensityPlayback() {
    if (!state.densityPlayTimer) {
        return;
    }
    clearInterval(state.densityPlayTimer);
    state.densityPlayTimer = null;
    setText("density-play", "播放");
}

function startDensityPlayback() {
    if (!state.densityResult || !state.densityResult.buckets || state.densityResult.buckets.length <= 1) {
        return;
    }
    stopDensityPlayback();
    setText("density-play", "暂停");
    state.densityPlayTimer = setInterval(() => {
        const next = (state.currentBucketIndex + 1) % state.densityResult.buckets.length;
        setDensityBucketIndex(next);
        if (state.densitySelectedCellKey) {
            const selected = state.densityCellMaps[state.currentBucketIndex]?.get(state.densitySelectedCellKey);
            updateTrendSummary(state.densitySelectedCellKey, selected || null);
            renderSelectedCellTrend();
        }
    }, 750);
}

async function runDensityQuery() {
    stopAllTaxiMode(true);
    const { startTime, endTime } = ensureTimeRange(qs("density-start").value, qs("density-end").value);
    const cellSizeMeters = Number(qs("density-cell-size").value);
    const intervalMinutes = Number(qs("density-interval").value);

    if (!Number.isFinite(cellSizeMeters) || cellSizeMeters <= 0) {
        throw new Error("网格大小必须大于 0");
    }
    if (!Number.isFinite(intervalMinutes) || intervalMinutes <= 0) {
        throw new Error("粒度必须大于 0");
    }

    // 请求字段约定：
    // 1) 始终传时间范围 + 网格参数；
    // 2) 若已框选区域，则附带区域边界，后端优先按框选区域分析；
    // 3) 若未框选区域，则不传区域字段，后端回退全图范围。
    const payload = {
        startTime,
        endTime,
        intervalMinutes,
        cellSizeMeters
    };
    if (state.region) {
        payload.minLon = state.region.minLon;
        payload.minLat = state.region.minLat;
        payload.maxLon = state.region.maxLon;
        payload.maxLat = state.region.maxLat;
    }

    const data = await requestJson("/api/density", {
        method: "POST",
        body: JSON.stringify(payload)
    });

    state.densityResult = data;
    state.currentBucketIndex = 0;
    buildDensityCellMaps();
    stopDensityPlayback();

    const bucketSelect = qs("density-bucket");
    bucketSelect.innerHTML = "";
    data.buckets.forEach((bucket, index) => {
        const option = document.createElement("option");
        option.value = String(index);
        option.textContent = `${formatDateTime(bucket.startTime)} - ${formatDateTime(bucket.endTime)}`;
        bucketSelect.appendChild(option);
    });
    bucketSelect.value = "0";

    const timeline = qs("density-timeline");
    timeline.min = "0";
    timeline.max = String(Math.max(0, data.buckets.length - 1));
    timeline.value = "0";

    const regionText = data.regionSource === "selection" ? "框选区域" : "全图范围";
    renderInfoPanel("density-info", [
        ["统计范围", regionText],
        ["覆盖车辆", formatCount(data.totalVehicleCount)],
        ["采样点", formatCount(data.totalPointCount)],
        ["时间桶", formatCount(data.bucketCount || data.buckets.length)],
        ["网格数", formatCount(data.gridCount || 0)],
        ["峰值密度", Number(data.maxVehicleDensity || 0).toFixed(2)],
        ["用时", `${Number(data.elapsedSeconds).toFixed(3)} s`]
    ]);

    updateDensityTimeLabel();
    updateTrendSummary(null, null);
    drawDensityBucket();
}
function applyDefaultTimeValues() {
    const defaultStart = "2008-02-03T06:30";
    const defaultEnd = "2008-02-03T22:00";
    qs("region-start").value = defaultStart;
    qs("region-end").value = defaultEnd;
    qs("density-start").value = defaultStart;
    qs("density-end").value = defaultEnd;
}

function attachButtonRipple(button) {
    button.addEventListener("pointerdown", (event) => {
        const rect = button.getBoundingClientRect();
        const ripple = document.createElement("span");
        ripple.className = "ripple";
        const size = Math.max(rect.width, rect.height) * 1.8;
        ripple.style.width = `${size}px`;
        ripple.style.height = `${size}px`;
        ripple.style.left = `${event.clientX - rect.left}px`;
        ripple.style.top = `${event.clientY - rect.top}px`;
        button.appendChild(ripple);
        ripple.addEventListener("animationend", () => ripple.remove(), { once: true });
    });
}

function bindEvents() {
    document.querySelectorAll(".tool-btn[data-panel]").forEach((button) => {
        button.addEventListener("click", () => {
            activateDockPanel(button.dataset.panel);
        });
    });

    document.querySelectorAll("button").forEach((button) => {
        attachButtonRipple(button);
    });

    const rail = document.querySelector(".rail");
    if (rail) {
        rail.addEventListener("dblclick", (event) => {
            if (event.target.closest("button, input, select, label, .result, .result-block")) {
                return;
            }
            clearDockSelection();
        });
    }

    qs("select-region-btn").addEventListener("click", startRegionSelection);

    qs("clear-region-btn").addEventListener("click", () => {
        cancelRegionSelection();
        state.region = null;
        clearRegionOverlay();
        clearTrajectoryOverlays();
        resetDensityState();
        updateRegionStatus();
    });

    qs("trajectory-btn").addEventListener("click", async () => {
        try {
            await runTrajectoryQuery();
        } catch (error) {
            renderInfoPanel("trajectory-info", [], error.message);
        }
    });

    qs("region-query-btn").addEventListener("click", async () => {
        try {
            await runRegionQuery();
        } catch (error) {
            renderInfoPanel("region-query-info", [], error.message);
        }
    });

    qs("density-btn").addEventListener("click", async () => {
        try {
            await runDensityQuery();
        } catch (error) {
            renderInfoPanel("density-info", [], error.message);
        }
    });

    qs("density-bucket").addEventListener("change", (event) => {
        setDensityBucketIndex(Number(event.target.value));
    });

    qs("density-timeline").addEventListener("input", (event) => {
        setDensityBucketIndex(Number(event.target.value));
    });

    qs("density-prev").addEventListener("click", () => {
        stopDensityPlayback();
        setDensityBucketIndex(state.currentBucketIndex - 1);
    });

    qs("density-next").addEventListener("click", () => {
        stopDensityPlayback();
        setDensityBucketIndex(state.currentBucketIndex + 1);
    });

    qs("density-play").addEventListener("click", () => {
        if (state.densityPlayTimer) {
            stopDensityPlayback();
            return;
        }
        startDensityPlayback();
    });
}

async function bootstrap() {
    setText("server-status", "连接中");
    try {
        state.meta = await requestJson("/api/meta");
        await loadBaiduMapScript(state.meta.baiduMapAk);
        initMap();
        ensureDensityCanvas();
        installDensityMapInteractions();
        installRegionSelection();
        bindEvents();
        applyDefaultTimeValues();
        updateMetaStatus();
        updateRegionStatus();
        updateModeStatus("地图");
        clearDockSelection();
    } catch (error) {
        setText("server-status", "失败");
        renderInfoPanel("trajectory-info", [], error.message);
        throw error;
    }
}

bootstrap().catch((error) => {
    console.error(error);
});


