import { state } from "../core/state.js";
import { qs, updateModeStatus, updateRegionStatus } from "../core/utils.js";
import { stopAllTaxiMode } from "../features/trajectory/trajectoryService.js";
import { resetDensityState } from "../features/density/densityStore.js";
import { clearRegionOverlay, setRegionSelectionMapLocked, pixelToPoint, renderRegion } from "./map.js";

function startRegionSelection() {
    stopAllTaxiMode(true);
    resetDensityState();
    hideSelectionBox();
    clearRegionOverlay();
    setRegionSelectionMapLocked(true);
    getSelectionLayer().classList.add("active");
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
    getSelectionLayer().classList.remove("active");
    setRegionSelectionMapLocked(false);
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

export {
    startRegionSelection,
    cancelRegionSelection,
    getSelectionLayer,
    getSelectionBox,
    updateSelectionBox,
    hideSelectionBox,
    installRegionSelection
};
