import { state, REGION_MAP_INTERACTIONS } from "../core/state.js";
import { qs, renderInfoPanel, setText } from "../core/utils.js";

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

    state.map.addEventListener("zoomend", () => {
        state.densityOverlay?.draw?.();
        state.allTaxiMode = state.allTaxiMode;
    });
    state.map.addEventListener("moveend", () => {
        state.densityOverlay?.draw?.();
    });

    window.addEventListener("resize", () => {
        state.densityOverlay?.draw?.();
        if (state.densityTrendPreviewChart) {
            state.densityTrendPreviewChart.resize();
        }
        if (state.densityTrendModalChart && state.densityTrendModalOpen) {
            state.densityTrendModalChart.resize();
        }
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

    if (state.allTaxiDotOverlay) {
        state.allTaxiDotOverlay.destroy?.();
        state.allTaxiDotOverlay = null;
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

function setRegionSelectionMapLocked(locked) {
    if (!state.map) {
        return;
    }

    for (const [disableMethod, enableMethod] of REGION_MAP_INTERACTIONS) {
        const method = locked ? disableMethod : enableMethod;
        if (typeof state.map[method] === "function") {
            state.map[method]();
        }
    }
}

function clearDensityOverlay() {
    state.densityOverlay?.clear?.();
}

function clearDensityCanvas() {
    clearDensityOverlay();
}

function clearAllTaxiDotOverlay() {
    state.allTaxiDotOverlay?.destroy?.();
    state.allTaxiDotOverlay = null;
}

function readValidMapBounds() {
    const bounds = state.map?.getBounds?.();
    if (!bounds || typeof bounds.getSouthWest !== "function" || typeof bounds.getNorthEast !== "function") {
        return null;
    }

    const sw = bounds.getSouthWest();
    const ne = bounds.getNorthEast();
    if (!sw || !ne) {
        return null;
    }

    const minLon = Number(sw.lng);
    const minLat = Number(sw.lat);
    const maxLon = Number(ne.lng);
    const maxLat = Number(ne.lat);
    if (![minLon, minLat, maxLon, maxLat].every(Number.isFinite)) {
        return null;
    }
    if (minLon >= maxLon || minLat >= maxLat) {
        return null;
    }

    return { minLon, minLat, maxLon, maxLat };
}

function redrawAllTaxiOverlay() {
    state.allTaxiDotOverlay?.draw?.();
    state.allTaxiPointCollection?.setStyle?.({ color: "#1f78ff" });
}

function requestDensityRedraw() {
    if (state.densityRedrawFrame) {
        cancelAnimationFrame(state.densityRedrawFrame);
    }
    state.densityRedrawFrame = requestAnimationFrame(() => {
        state.densityRedrawFrame = null;
        state.densityOverlay?.draw?.();
    });
}

function cancelDensityRedraw() {
    if (state.densityRedrawFrame) {
        cancelAnimationFrame(state.densityRedrawFrame);
        state.densityRedrawFrame = null;
    }
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

export {
    loadBaiduMapScript,
    initMap,
    clearTrajectoryOverlays,
    addTrajectoryOverlay,
    clearRegionOverlay,
    setRegionSelectionMapLocked,
    clearDensityOverlay,
    clearDensityCanvas,
    clearAllTaxiDotOverlay,
    readValidMapBounds,
    redrawAllTaxiOverlay,
    requestDensityRedraw,
    cancelDensityRedraw,
    clearDensityQueryState,
    clearDensityCacheOnly,
    resetDensityState,
    createRegionPolygon,
    renderRegion,
    pointToOverlayPixel,
    pixelToPoint
};

