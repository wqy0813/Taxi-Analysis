import { state } from "../../core/state.js";
import { qs } from "../../core/utils.js";
import { pointToOverlayPixel } from "../map.js";
import { getDensityCellBounds, getHeatColor } from "../../features/density/densityStore.js";
import { ensureDensityTrendUi } from "../../features/density/densityChart.js";

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

    if (!state.densityMeta || !state.densityBucketCache.has(state.currentBucketIndex)) {
        return;
    }

    const bucket = state.densityBucketCache.get(state.currentBucketIndex);
    if (!bucket || !bucket.cells) {
        return;
    }

    this._ctx.save();
    this._ctx.globalCompositeOperation = "source-over";
    this._ctx.lineJoin = "miter";
    this._ctx.lineCap = "square";

    for (const cell of bucket.cells) {
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
        this._ctx.fillStyle = cellColor;
        this._ctx.fillRect(left, top, drawWidth, drawHeight);
    }

    if (state.densitySelectedCellKey) {
        const selected = state.densityCellMaps.get(state.currentBucketIndex)?.get(state.densitySelectedCellKey);
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

function TaxiDotOverlay(points) {
    this._points = Array.isArray(points) ? points : [];
    this._map = null;
    this._container = null;
    this._canvas = null;
    this._ctx = null;
    this._cssWidth = 0;
    this._cssHeight = 0;
    this._dpr = 1;
}

TaxiDotOverlay.prototype.attach = function attach(map) {
    this._map = map;

    const container = document.createElement("div");
    container.className = "taxi-dot-overlay-layer";
    container.style.position = "absolute";
    container.style.left = "0";
    container.style.top = "0";
    container.style.right = "0";
    container.style.bottom = "0";
    container.style.zIndex = "7";
    container.style.pointerEvents = "none";

    const canvas = document.createElement("canvas");
    canvas.className = "taxi-dot-overlay-canvas";
    canvas.style.display = "block";
    canvas.style.width = "100%";
    canvas.style.height = "100%";
    container.appendChild(canvas);

    const hostContainer = typeof map.getContainer === "function" ? map.getContainer() : null;
    if (!hostContainer) {
        throw new Error("百度地图容器未就绪");
    }
    const parentStyle = hostContainer.style;
    if (parentStyle.position === "") {
        parentStyle.position = "relative";
    }
    hostContainer.appendChild(container);

    this._container = container;
    this._canvas = canvas;
    this._ctx = canvas.getContext("2d");
    this.syncSize();
    this.draw();
    return container;
};

TaxiDotOverlay.prototype.setPoints = function setPoints(points) {
    this._points = Array.isArray(points) ? points : [];
    this.draw();
};

TaxiDotOverlay.prototype.syncSize = function syncSize() {
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

TaxiDotOverlay.prototype.clear = function clear() {
    if (!this._ctx || !this._canvas) {
        return;
    }
    this._ctx.setTransform(1, 0, 0, 1, 0, 0);
    this._ctx.clearRect(0, 0, this._canvas.width, this._canvas.height);
    this._ctx.scale(this._dpr || 1, this._dpr || 1);
};

TaxiDotOverlay.prototype.destroy = function destroy() {
    this.clear();
    if (this._container && this._container.parentNode) {
        this._container.parentNode.removeChild(this._container);
    }
    this._container = null;
    this._canvas = null;
    this._ctx = null;
    this._map = null;
};

TaxiDotOverlay.prototype.draw = function draw() {
    this.syncSize();
    this.render();
};

TaxiDotOverlay.prototype.render = function render() {
    if (!this._map || !this._ctx) {
        return;
    }

    this.clear();
    if (!Array.isArray(this._points) || this._points.length === 0) {
        return;
    }

    const ctx = this._ctx;
    ctx.save();
    ctx.globalCompositeOperation = "source-over";
    ctx.fillStyle = "#1f78ff";
    ctx.strokeStyle = "#ffffff";
    ctx.lineWidth = 1;

    for (const point of this._points) {
        const pixel = pointToOverlayPixel(point.lng, point.lat);
        if (!pixel) {
            continue;
        }
        const x = pixel.x;
        const y = pixel.y;
        if (x < -6 || y < -6 || x > this._cssWidth + 6 || y > this._cssHeight + 6) {
            continue;
        }
        ctx.beginPath();
        ctx.arc(x, y, 2.1, 0, Math.PI * 2);
        ctx.fill();
    }

    ctx.restore();
};

function clearAllTaxiDotOverlay() {
    state.allTaxiDotOverlay?.destroy?.();
    state.allTaxiDotOverlay = null;
}

function ensureDensityOverlay() {
    state.densityTrendPreviewEl = qs("density-trend-preview");
    state.densityTrendModalEl = qs("density-trend-modal-chart");
    state.densityTrendModal = qs("density-trend-modal");
    state.densityTrendModalTitle = qs("density-trend-modal-title");
    state.densityTrendModalSubtitle = qs("density-trend-modal-subtitle");
    state.densityTrendModalClose = qs("density-trend-modal-close");
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
    ensureDensityTrendUi();
}

export {
    DensityGridOverlay,
    TaxiDotOverlay,
    clearAllTaxiDotOverlay,
    ensureDensityOverlay
};
