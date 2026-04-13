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
    densityOverlay: null,
    densityCellMaps: [],
    densityPlayTimer: null,
    densityHoverCell: null,
    densitySelectedCellKey: null,
    densityTrendCanvas: null,
    densityTrendCtx: null,
    densityTooltip: null,
    densityRedrawFrame: null,
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
        syncDensityTrendCanvasSize();
        requestDensityRedraw();
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

function clearDensityOverlay() {
    // 这里不清空分析结果，只清空当前覆盖物的可视内容。
    // 这样用户切换到轨迹/区域查询时，密度层会暂时隐藏，但数据状态仍可保留，
    // 便于后续继续切换时间桶或重新进入密度视图时直接重绘。
    if (state.densityOverlay && typeof state.densityOverlay.clear === "function") {
        state.densityOverlay.clear();
    }
}

function clearDensityCanvas() {
    clearDensityOverlay();
}

function requestDensityRedraw() {
    if (state.densityRedrawFrame) {
        cancelAnimationFrame(state.densityRedrawFrame);
        state.densityRedrawFrame = null;
    }
    state.densityRedrawFrame = requestAnimationFrame(() => {
        state.densityRedrawFrame = null;
        if (state.densityOverlay && typeof state.densityOverlay.draw === "function") {
            state.densityOverlay.draw();
        }
    });
}

function cancelDensityRedraw() {
    if (state.densityRedrawFrame) {
        cancelAnimationFrame(state.densityRedrawFrame);
        state.densityRedrawFrame = null;
    }
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
    cancelDensityRedraw();
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
    clearDensityOverlay();
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

// 密度网格使用百度地图自定义 Overlay 绘制，而不是在页面上叠一个独立 canvas。
// 旧方案的问题在于：
// 1. 页面 canvas 和地图覆盖物并不天然共享同一坐标系，缩放/平移后容易出现偏移；
// 2. 单独 canvas 需要手动同步尺寸和像素比，边缘网格很容易因为 1px 以下被误判为“看不见”；
// 3. 地图交互时，普通 canvas 不会跟随地图覆盖物体系自动重排，局部缺失和断裂更明显。
//
// 新方案把绘制放进 BMap.Overlay：
// - 底图网格按完整规则网格绘制，不依赖 bucket.cells 是否有数据；
// - 有数据的格子在底图之上做热力填色，形成“网格 + 热力”双层表达；
// - 坐标换算统一使用 pointToOverlayPixel()；
// - 当网格投影尺寸小于 1px 时改为最小可见绘制，而不是直接跳过，避免缩放后“断格”。
function DensityGridOverlay() {
    this._map = null;
    this._container = null;
    this._canvas = null;
    this._ctx = null;
    this._cssWidth = 0;
    this._cssHeight = 0;
    this._dpr = 1;
}

DensityGridOverlay.prototype.initialize = function initialize(map) {
    this._map = map;

    const container = document.createElement("div");
    container.className = "density-overlay-layer";
    container.style.position = "absolute";
    container.style.left = "0";
    container.style.top = "0";
    container.style.right = "0";
    container.style.bottom = "0";
    container.style.zIndex = "8";
    container.style.pointerEvents = "none";

    const canvas = document.createElement("canvas");
    canvas.className = "density-overlay-canvas";
    canvas.style.display = "block";
    canvas.style.width = "100%";
    canvas.style.height = "100%";
    container.appendChild(canvas);

    const panes = typeof map.getPanes === "function" ? map.getPanes() : null;
    const hostPane = panes?.floatPane || panes?.labelPane || panes?.markerPane;
    if (!hostPane) {
        throw new Error("百度地图覆盖物容器未就绪");
    }
    hostPane.appendChild(container);

    this._container = container;
    this._canvas = canvas;
    this._ctx = canvas.getContext("2d");
    this.syncSize();
    return container;
};

DensityGridOverlay.prototype.syncSize = function syncSize() {
    if (!this._map || !this._container || !this._canvas || !this._ctx) {
        return;
    }

    const size = this._map.getSize();
    if (!size) {
        return;
    }

    const cssWidth = Math.max(1, Math.round(size.width || 0));
    const cssHeight = Math.max(1, Math.round(size.height || 0));
    const dpr = Math.max(1, window.devicePixelRatio || 1);
    const pixelWidth = Math.max(1, Math.round(cssWidth * dpr));
    const pixelHeight = Math.max(1, Math.round(cssHeight * dpr));

    this._container.style.width = `${cssWidth}px`;
    this._container.style.height = `${cssHeight}px`;
    this._canvas.style.width = `${cssWidth}px`;
    this._canvas.style.height = `${cssHeight}px`;

    if (this._canvas.width !== pixelWidth) {
        this._canvas.width = pixelWidth;
    }
    if (this._canvas.height !== pixelHeight) {
        this._canvas.height = pixelHeight;
    }

    this._cssWidth = cssWidth;
    this._cssHeight = cssHeight;
    this._dpr = dpr;

    this._ctx.setTransform(1, 0, 0, 1, 0, 0);
    this._ctx.clearRect(0, 0, this._canvas.width, this._canvas.height);
    this._ctx.scale(dpr, dpr);
};

DensityGridOverlay.prototype.clear = function clear() {
    if (!this._ctx || !this._canvas) {
        return;
    }
    this._ctx.setTransform(1, 0, 0, 1, 0, 0);
    this._ctx.clearRect(0, 0, this._canvas.width, this._canvas.height);
    this._ctx.scale(this._dpr || 1, this._dpr || 1);
};

DensityGridOverlay.prototype.draw = function draw() {
    this.syncSize();
    this.render();
};

DensityGridOverlay.prototype.render = function render() {
    if (!this._map || !this._ctx) {
        return;
    }

    this.clear();

    if (!state.densityResult || !state.densityResult.buckets || !state.densityResult.buckets.length) {
        return;
    }

    const bucket = state.densityResult.buckets[state.currentBucketIndex];
    if (!bucket) {
        return;
    }

    const visibleRange = getVisibleDensityGridRange();
    this._ctx.save();
    this._ctx.globalCompositeOperation = "source-over";
    this._ctx.lineJoin = "miter";
    this._ctx.lineCap = "square";

    drawDensityGridBase(this._ctx, visibleRange);
    drawDensityHeatCells(this._ctx, bucket.cells || [], visibleRange);

    if (state.densitySelectedCellKey) {
        const selected = getDensityCellByKey(state.currentBucketIndex, state.densitySelectedCellKey);
        if (selected) {
            const bounds = getDensityCellBounds(selected);
            if (bounds) {
                const topLeft = pointToOverlayPixel(bounds.minLon, bounds.maxLat);
                const bottomRight = pointToOverlayPixel(bounds.maxLon, bounds.minLat);
                const centerX = (topLeft.x + bottomRight.x) * 0.5;
                const centerY = (topLeft.y + bottomRight.y) * 0.5;
                const drawWidth = Math.max(1, Math.abs(bottomRight.x - topLeft.x));
                const drawHeight = Math.max(1, Math.abs(bottomRight.y - topLeft.y));
                const left = Math.round(centerX - drawWidth * 0.5);
                const top = Math.round(centerY - drawHeight * 0.5);
                this._ctx.strokeStyle = "rgba(19, 52, 71, 0.92)";
                this._ctx.lineWidth = 2;
                this._ctx.strokeRect(left + 0.5, top + 0.5, Math.max(0, Math.round(drawWidth) - 1), Math.max(0, Math.round(drawHeight) - 1));
            }
        }
    }

    this._ctx.restore();
};

function renderRegion(region) {
    clearRegionOverlay();
    state.regionPolygon = createRegionPolygon(region);
    state.map.addOverlay(state.regionPolygon);
}

function ensureDensityOverlay() {
    state.densityTrendCanvas = qs("density-trend-canvas");
    state.densityTrendCtx = state.densityTrendCanvas.getContext("2d");
    state.densityTooltip = qs("density-tooltip");
    if (!state.densityOverlay) {
        try {
            if (window.BMap && window.BMap.Overlay && Object.getPrototypeOf(DensityGridOverlay.prototype) !== BMap.Overlay.prototype) {
                Object.setPrototypeOf(DensityGridOverlay.prototype, BMap.Overlay.prototype);
            }
            state.densityOverlay = new DensityGridOverlay();
            state.map.addOverlay(state.densityOverlay);
        } catch (error) {
            console.warn("density overlay init failed:", error);
            state.densityOverlay = null;
        }
    }
    syncDensityTrendCanvasSize();
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

function pointToOverlayPixel(lon, lat) {
    const point = new BMap.Point(lon, lat);
    if (state.map && typeof state.map.pointToOverlayPixel === "function") {
        return state.map.pointToOverlayPixel(point);
    }
    return state.map.pointToPixel(point);
}

function pixelToPoint(x, y) {
    return state.map.pixelToPoint(new BMap.Pixel(x, y));
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
    const bucket = state.densityCellMaps[bucketIndex];
    const keyText = String(key || "");
    const cached = bucket?.get(keyText);
    if (cached) {
        return cached;
    }

    const grid = parseDensityCellKey(keyText);
    if (!grid) {
        return null;
    }

    // 空格子也需要能被命中和高亮，方便“完整网格 + 热力填色”的交互保持一致。
    return {
        gx: grid.gx,
        gy: grid.gy,
        pointCount: 0,
        vehicleCount: 0,
        vehicleDensity: 0,
        flowIntensity: 0,
        deltaVehicleCount: 0,
        deltaVehicleDensity: 0,
        deltaRate: 0
    };
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

function drawDensityGridBase(ctx, visibleRange) {
    if (!state.densityResult) {
        return;
    }

    const minLon = Number(state.densityResult.minLon);
    const minLat = Number(state.densityResult.minLat);
    const maxLon = Number(state.densityResult.maxLon);
    const maxLat = Number(state.densityResult.maxLat);
    const lonStep = Number(state.densityResult.lonStep);
    const latStep = Number(state.densityResult.latStep);
    const columnCount = Number(state.densityResult.columnCount || 0);
    const rowCount = Number(state.densityResult.rowCount || 0);

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

        // 热力填色采用最小可见尺寸，而不是直接跳过细网格。
        // 这样在缩放较远时，仍然能看见网格结构，不会出现“空白断层”。
        const drawWidth = Math.max(1, rawWidth);
        const drawHeight = Math.max(1, rawHeight);
        const left = Math.round(centerX - drawWidth * 0.5);
        const top = Math.round(centerY - drawHeight * 0.5);

        const ratio = getDensityRatio(cell);
        const cellColor = getHeatColor(ratio);
        ctx.fillStyle = cellColor;
        ctx.fillRect(left, top, drawWidth, drawHeight);

        // 有数据格子的边框比底图网格略强一层，保持规则网格感，同时让热力信息更显眼。
        ctx.strokeStyle = ratio > 0.72 ? "rgba(214, 71, 64, 0.70)" : "rgba(80, 146, 176, 0.38)";
        ctx.lineWidth = 1;
        ctx.strokeRect(left + 0.5, top + 0.5, Math.max(0, Math.round(drawWidth) - 1), Math.max(0, Math.round(drawHeight) - 1));
    }

    ctx.restore();
}

function getVisibleDensityGridRange() {
    if (!state.densityResult || !state.map || typeof state.map.getBounds !== "function") {
        return null;
    }

    const bounds = state.map.getBounds();
    if (!bounds || typeof bounds.getSouthWest !== "function" || typeof bounds.getNorthEast !== "function") {
        return null;
    }

    const sw = bounds.getSouthWest();
    const ne = bounds.getNorthEast();
    const minLon = Number(state.densityResult.minLon);
    const minLat = Number(state.densityResult.minLat);
    const maxLon = Number(state.densityResult.maxLon);
    const maxLat = Number(state.densityResult.maxLat);
    const lonStep = Number(state.densityResult.lonStep);
    const latStep = Number(state.densityResult.latStep);
    const columnCount = Number(state.densityResult.columnCount || 0);
    const rowCount = Number(state.densityResult.rowCount || 0);

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

    if (values.length <= 0) {
        return;
    }

    const stepX = values.length === 1 ? 0 : (width - padding * 2) / (values.length - 1);
    state.densityTrendCtx.strokeStyle = "rgba(54, 150, 142, 0.92)";
    state.densityTrendCtx.lineWidth = 2;
    state.densityTrendCtx.beginPath();
    values.forEach((value, index) => {
        const x = padding + stepX * index;
        const ratio = maxValue <= 0 ? 0 : value / maxValue;
        const y = height - padding - ratio * (height - padding * 2);
        if (index === 0) {
            state.densityTrendCtx.moveTo(x, y);
        } else {
            state.densityTrendCtx.lineTo(x, y);
        }
    });
    state.densityTrendCtx.stroke();

    values.forEach((value, index) => {
        const x = padding + stepX * index;
        const ratio = maxValue <= 0 ? 0 : value / maxValue;
        const y = height - padding - ratio * (height - padding * 2);
        state.densityTrendCtx.fillStyle = index === state.currentBucketIndex
            ? "rgba(223, 79, 73, 0.95)"
            : "rgba(54, 150, 142, 0.9)";
        state.densityTrendCtx.beginPath();
        state.densityTrendCtx.arc(x, y, 3, 0, Math.PI * 2);
        state.densityTrendCtx.fill();
    });
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

function updateDensityTimeLabel() {
    const bucket = getCurrentBucket();
    setText("density-time-label", getBucketLabel(bucket));
}

function setDensityBucketIndex(index) {
    if (!state.densityResult || !state.densityResult.buckets || state.densityResult.buckets.length === 0) {
        return;
    }
    const count = state.densityResult.buckets.length;
    const nextIndex = ((Number(index) % count) + count) % count;
    state.currentBucketIndex = nextIndex;
    qs("density-bucket").value = String(nextIndex);
    qs("density-timeline").value = String(nextIndex);
    updateDensityTimeLabel();
    drawDensityBucket();

    if (state.densitySelectedCellKey) {
        const selected = getDensityCellByKey(state.currentBucketIndex, state.densitySelectedCellKey);
        updateTrendSummary(state.densitySelectedCellKey, selected || null);
        renderSelectedCellTrend();
    }
}

function startRegionSelection() {
    stopAllTaxiMode(true);
    resetDensityState();
    hideSelectionBox();
    clearRegionOverlay();
    state.selectingRegion = true;
    state.selectionStartPixel = null;
    state.selectionEndPixel = null;
    updateModeStatus("框选区域");
}

function cancelRegionSelection() {
    state.selectingRegion = false;
    state.selectionStartPixel = null;
    state.selectionEndPixel = null;
    hideSelectionBox();
}

function getSelectionLayer() {
    if (state.selectionLayer) {
        return state.selectionLayer;
    }
    const mapElement = qs("map");
    const layer = document.createElement("div");
    layer.className = "selection-layer";
    layer.innerHTML = `<div class="selection-box" id="selection-box"></div>`;
    mapElement.appendChild(layer);
    state.selectionLayer = layer;
    return layer;
}

function getSelectionBox() {
    getSelectionLayer();
    return document.getElementById("selection-box");
}

function updateSelectionBox(startPixel, endPixel) {
    const box = getSelectionBox();
    const left = Math.min(startPixel.x, endPixel.x);
    const top = Math.min(startPixel.y, endPixel.y);
    const width = Math.abs(startPixel.x - endPixel.x);
    const height = Math.abs(startPixel.y - endPixel.y);

    box.style.display = "block";
    box.style.left = `${left}px`;
    box.style.top = `${top}px`;
    box.style.width = `${width}px`;
    box.style.height = `${height}px`;
}

function hideSelectionBox() {
    const box = getSelectionBox();
    box.style.display = "none";
}

function installRegionSelection() {
    const mapElement = qs("map");
    getSelectionLayer();

    mapElement.addEventListener("mousedown", (event) => {
        if (!state.selectingRegion || event.button !== 0) {
            return;
        }
        const rect = mapElement.getBoundingClientRect();
        state.selectionStartPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        state.selectionEndPixel = { ...state.selectionStartPixel };
        updateSelectionBox(state.selectionStartPixel, state.selectionEndPixel);
        event.preventDefault();
    });

    mapElement.addEventListener("mousemove", (event) => {
        if (!state.selectingRegion || !state.selectionStartPixel) {
            return;
        }
        const rect = mapElement.getBoundingClientRect();
        state.selectionEndPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        updateSelectionBox(state.selectionStartPixel, state.selectionEndPixel);
        event.preventDefault();
    });

    window.addEventListener("mouseup", (event) => {
        if (!state.selectingRegion || !state.selectionStartPixel || !state.selectionEndPixel) {
            return;
        }
        const rect = mapElement.getBoundingClientRect();
        const endPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        state.selectionEndPixel = endPixel;

        const width = Math.abs(state.selectionStartPixel.x - endPixel.x);
        const height = Math.abs(state.selectionStartPixel.y - endPixel.y);
        if (width < 8 || height < 8) {
            cancelRegionSelection();
            updateModeStatus("地图");
            return;
        }

        const leftTop = pixelToPoint(
            Math.min(state.selectionStartPixel.x, endPixel.x),
            Math.min(state.selectionStartPixel.y, endPixel.y)
        );
        const rightBottom = pixelToPoint(
            Math.max(state.selectionStartPixel.x, endPixel.x),
            Math.max(state.selectionStartPixel.y, endPixel.y)
        );

        state.region = {
            minLon: leftTop.lng,
            maxLon: rightBottom.lng,
            maxLat: leftTop.lat,
            minLat: rightBottom.lat
        };

        cancelRegionSelection();
        renderRegion(state.region);
        updateRegionStatus();
        updateModeStatus("区域已锁定");
    });
}

async function runTrajectoryQuery() {
    stopAllTaxiMode(true);
    ensureRegion();
    const taxiId = qs("trajectory-taxi-id").value.trim();
    const { startTime, endTime } = ensureTimeRange(qs("region-start").value, qs("region-end").value);

    if (!taxiId) {
        throw new Error("请输入出租车 ID");
    }

    const data = await requestJson("/api/trajectory", {
        method: "POST",
        body: JSON.stringify({
            taxiId,
            startTime,
            endTime,
            minLon: state.region.minLon,
            minLat: state.region.minLat,
            maxLon: state.region.maxLon,
            maxLat: state.region.maxLat
        })
    });

    clearTrajectoryOverlays();
    resetDensityState();
    renderRegion(state.region);

    const color = "#13b4b0";
    const path = data.points.map((point) => new BMap.Point(point.lon, point.lat));
    if (path.length > 0) {
        const polyline = new BMap.Polyline(path, {
            strokeColor: color,
            strokeWeight: 4,
            strokeOpacity: 0.86
        });
        addTrajectoryOverlay(polyline);

        const startMarker = new BMap.Marker(path[0], {
            icon: new BMap.Symbol(BMap_Symbol_SHAPE_POINT, {
                scale: 1.2,
                fillColor: "#1f78ff",
                fillOpacity: 0.95,
                strokeColor: "#ffffff",
                strokeWeight: 2
            })
        });
        const endMarker = new BMap.Marker(path[path.length - 1], {
            icon: new BMap.Symbol(BMap_Symbol_SHAPE_POINT, {
                scale: 1.2,
                fillColor: "#eb5757",
                fillOpacity: 0.95,
                strokeColor: "#ffffff",
                strokeWeight: 2
            })
        });
        addTrajectoryOverlay(startMarker);
        addTrajectoryOverlay(endMarker);
    }

    renderInfoPanel("trajectory-info", [
        ["出租车", data.taxiId],
        ["点数", formatCount(data.pointCount)],
        ["起点", data.pointCount ? `${data.points[0].lon.toFixed(5)}, ${data.points[0].lat.toFixed(5)}` : "-"],
        ["终点", data.pointCount ? `${data.points[data.points.length - 1].lon.toFixed(5)}, ${data.points[data.points.length - 1].lat.toFixed(5)}` : "-"],
        ["用时", `${Number(data.elapsedSeconds).toFixed(3)} s`]
    ]);

    updateModeStatus("轨迹");
}

async function runRegionQuery() {
    stopAllTaxiMode(true);
    ensureRegion();
    const { startTime, endTime } = ensureTimeRange(qs("region-start").value, qs("region-end").value);

    const data = await requestJson("/api/region", {
        method: "POST",
        body: JSON.stringify({
            startTime,
            endTime,
            minLon: state.region.minLon,
            minLat: state.region.minLat,
            maxLon: state.region.maxLon,
            maxLat: state.region.maxLat
        })
    });

    clearTrajectoryOverlays();
    resetDensityState();
    renderRegion(state.region);

    renderInfoPanel("region-query-info", [
        ["车辆数", formatCount(data.vehicleCount)],
        ["轨迹点", formatCount(data.pointCount)],
        ["时间范围", `${formatDateTime(startTime)} - ${formatDateTime(endTime)}`],
        ["用时", `${Number(data.elapsedSeconds).toFixed(3)} s`]
    ]);

    updateModeStatus("区域统计");
}

function createPointCollection(points) {
    if (!window.BMapLib || !window.BMapLib.PointCollection) {
        throw new Error("PointCollection 组件未加载");
    }

    const pointCollection = new BMapLib.PointCollection(points, {
        size: BMAP_POINT_SIZE_TINY,
        shape: BMAP_POINT_SHAPE_CIRCLE,
        color: "rgba(35, 185, 177, 0.92)"
    });
    pointCollection.addEventListener("click", (event) => {
        const point = event.point;
        renderInfoPanel("trajectory-info", [
            ["经度", point.lng.toFixed(6)],
            ["纬度", point.lat.toFixed(6)],
            ["全图采样点", formatCount(state.currentAllTaxiPointCount)]
        ]);
    });
    return pointCollection;
}

function buildClusterBuckets(points) {
    const zoom = state.map.getZoom();
    const cellSize = zoom >= 16 ? 18 : zoom >= 14 ? 28 : zoom >= 12 ? 42 : 56;
    const buckets = new Map();

    for (const point of points) {
        const pixel = pointToOverlayPixel(point.lng, point.lat);
        const key = `${Math.floor(pixel.x / cellSize)}:${Math.floor(pixel.y / cellSize)}`;
        let bucket = buckets.get(key);
        if (!bucket) {
            bucket = {
                count: 0,
                sumLon: 0,
                sumLat: 0
            };
            buckets.set(key, bucket);
        }
        bucket.count += 1;
        bucket.sumLon += point.lng;
        bucket.sumLat += point.lat;
    }

    return Array.from(buckets.values()).map((bucket) => ({
        lng: bucket.sumLon / bucket.count,
        lat: bucket.sumLat / bucket.count,
        count: bucket.count
    }));
}

function createClusterMarker(cluster) {
    const ratio = Math.min(1, Math.log10(cluster.count + 1) / 4);
    const radius = 12 + ratio * 20;
    const fontSize = 12 + ratio * 4;
    const html = `
        <div class="cluster-bubble" style="
            width:${radius * 2}px;
            height:${radius * 2}px;
            line-height:${radius * 2}px;
            font-size:${fontSize}px;
            opacity:${0.68 + ratio * 0.22};
        ">${cluster.count}</div>
    `;
    return new BMap.Marker(new BMap.Point(cluster.lng, cluster.lat), {
        icon: new BMap.DivIcon({
            html,
            className: "cluster-marker",
            iconSize: new BMap.Size(radius * 2, radius * 2),
            iconAnchor: new BMap.Size(radius, radius)
        })
    });
}

function scheduleAllTaxiRefresh() {
    if (!state.allTaxiMode) {
        return;
    }
    if (state.allTaxiRefreshTimer) {
        clearTimeout(state.allTaxiRefreshTimer);
    }
    state.allTaxiRefreshTimer = setTimeout(() => {
        if (state.allTaxiMode) {
            runAllTaxiQuery(false).catch((error) => {
                renderInfoPanel("trajectory-info", [], error.message);
            });
        }
    }, 120);
}

function stopAllTaxiMode(clearOverlay = false) {
    state.allTaxiMode = false;
    state.allTaxiRequestSeq += 1;
    if (state.allTaxiRefreshTimer) {
        clearTimeout(state.allTaxiRefreshTimer);
        state.allTaxiRefreshTimer = null;
    }
    state.currentAllTaxiPointCount = 0;
    if (clearOverlay) {
        clearTrajectoryOverlays();
    }
}

async function runAllTaxiQuery(enableMode = true) {
    const requestSeq = ++state.allTaxiRequestSeq;
    const bounds = state.map.getBounds();
    const sw = bounds.getSouthWest();
    const ne = bounds.getNorthEast();

    const data = await requestJson("/api/all_taxi_points", {
        method: "POST",
        body: JSON.stringify({
            minLon: sw.lng,
            minLat: sw.lat,
            maxLon: ne.lng,
            maxLat: ne.lat
        })
    });

    if (requestSeq !== state.allTaxiRequestSeq) {
        return;
    }

    if (enableMode) {
        state.allTaxiMode = true;
    }

    clearTrajectoryOverlays();
    clearRegionOverlay();
    resetDensityState();

    const points = data.points.map((point) => new BMap.Point(point.lon, point.lat));
    state.currentAllTaxiPointCount = points.length;

    if (points.length === 0) {
        renderInfoPanel("trajectory-info", [
            ["全图点数", "0"],
            ["视野范围", "当前范围内无数据"],
            ["渲染模式", state.currentAllTaxiRenderMode === "point" ? "海量点" : "聚合"]
        ]);
        updateModeStatus("全图车辆");
        return;
    }

    const zoom = state.map.getZoom();
    const usePointMode = zoom >= 15 && points.length <= 25000;
    state.currentAllTaxiRenderMode = usePointMode ? "point" : "cluster";

    if (usePointMode && window.BMapLib && window.BMapLib.PointCollection) {
        state.allTaxiPointCollection = createPointCollection(points);
        state.map.addOverlay(state.allTaxiPointCollection);
    } else {
        const clusters = buildClusterBuckets(points);
        clusters.forEach((cluster) => {
            addTrajectoryOverlay(createClusterMarker(cluster));
        });
    }

    renderInfoPanel("trajectory-info", [
        ["全图点数", formatCount(points.length)],
        ["当前缩放", String(zoom)],
        ["渲染模式", usePointMode ? "海量点" : "聚合"],
        ["用时", `${Number(data.elapsedSeconds).toFixed(3)} s`]
    ]);

    updateModeStatus("全图车辆");
}

function tryGetCellByPoint(point) {
    if (!state.densityResult || !point) {
        return null;
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
        point.lng < minLon || point.lng > maxLon ||
        point.lat < minLat || point.lat > maxLat) {
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
    const cell = getDensityCellByKey(state.currentBucketIndex, key);
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
        const pixel = pointToOverlayPixel(event.point.lng, event.point.lat);
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
            const selected = getDensityCellByKey(state.currentBucketIndex, state.densitySelectedCellKey);
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
        ensureDensityOverlay();
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