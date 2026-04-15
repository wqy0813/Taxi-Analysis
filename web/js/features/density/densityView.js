import { state } from "../../core/state.js";
import { renderInfoPanel, escapeHtml } from "../../core/utils.js";
import { pointToOverlayPixel } from "../../map/map.js";
import {
    getDensityGridModel,
    getDensityCellByKey,
    getCurrentBucket,
    getBucketLabel,
    hideDensityTooltip,
    showDensityTooltip,
    drawDensityBucket
} from "./densityStore.js";
import { clearDensityTrendPreview, clearDensityTrendModal, renderSelectedCellTrend } from "./densityChart.js";

function tryGetCellByPoint(point) {
    const grid = getDensityGridModel();
    if (!grid || !point) {
        return null;
    }

    if (point.lng < grid.minLon || point.lng > grid.maxLon ||
        point.lat < grid.minLat || point.lat > grid.maxLat) {
        return null;
    }

    const gx = Math.max(0, Math.min(
        grid.columnCount - 1,
        Math.floor((point.lng - grid.minLon) / grid.lonStep)
    ));
    const gy = Math.max(0, Math.min(
        grid.rowCount - 1,
        Math.floor((point.lat - grid.minLat) / grid.latStep)
    ));

    const key = `${gx}:${gy}`;
    const cell = getDensityCellByKey(state.currentBucketIndex, key);
    return cell ? { key, cell } : null;
}

function updateTrendSummary(key, cell) {
    const bucket = getCurrentBucket();
    if (!key || !cell || !bucket) {
        renderInfoPanel("density-trend-summary", [], "\u70b9\u51fb\u7f51\u683c\u67e5\u770b\u8d8b\u52bf");
        return;
    }

    renderInfoPanel("density-trend-summary", [
        ["\u7f51\u683c", key],
        ["\u65f6\u95f4\u6bb5", getBucketLabel(bucket)],
        ["\u5bc6\u5ea6", Number(cell.vehicleDensity || 0).toFixed(2)]
    ]);
}

function installDensityMapInteractions() {
    state.map.addEventListener("mousemove", (event) => {
        if (!state.densityMeta) {
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
            `<div><strong>\u7f51\u683c:</strong> ${escapeHtml(hit.key)}</div>`,
            `<div><strong>\u65f6\u95f4\u6bb5:</strong> ${escapeHtml(getBucketLabel(bucket))}</div>`,
            `<div><strong>\u5bc6\u5ea6:</strong> ${escapeHtml(Number(hit.cell.vehicleDensity || 0).toFixed(2))}</div>`
        ].join("");
        const pixel = pointToOverlayPixel(event.point.lng, event.point.lat);
        showDensityTooltip(html, pixel.x, pixel.y);
    });

    state.map.addEventListener("mouseout", () => {
        hideDensityTooltip();
    });

    state.map.addEventListener("click", (event) => {
        if (!state.densityMeta) {
            return;
        }

        const hit = tryGetCellByPoint(event.point);
        if (!hit) {
            state.densitySelectedCellKey = null;
            updateTrendSummary(null, null);
            clearDensityTrendPreview();
            clearDensityTrendModal();
            drawDensityBucket();
            return;
        }

        state.densitySelectedCellKey = hit.key;
        updateTrendSummary(hit.key, hit.cell);
        renderSelectedCellTrend();
        drawDensityBucket();
    });
}

export {
    tryGetCellByPoint,
    updateTrendSummary,
    installDensityMapInteractions
};
