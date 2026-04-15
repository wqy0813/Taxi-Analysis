import { state } from "../../core/state.js";
import { qs, renderInfoPanel, requestJson } from "../../core/utils.js";
import { clearTrajectoryOverlays, readValidMapBounds } from "../../map/map.js";
import { renderAllTaxiTrajectoryResult, renderSingleTrajectoryResult } from "./trajectoryView.js";

function scheduleAllTaxiRefresh() {
    if (!state.allTaxiMode) {
        return;
    }
    const zoom = state.map?.getZoom?.() ?? 12;
    const delay = zoom >= 18 ? 800 : 240;
    if (state.allTaxiRefreshTimer) {
        clearTimeout(state.allTaxiRefreshTimer);
    }
    state.allTaxiRefreshTimer = setTimeout(() => {
        if (state.allTaxiMode) {
            runAllTaxiQuery(false).catch((error) => {
                renderInfoPanel("trajectory-info", [], error.message);
            });
        }
    }, delay);
}

function stopAllTaxiMode(clearOverlay = false) {
    state.allTaxiMode = false;
    state.allTaxiRequestSeq += 1;
    if (state.allTaxiRefreshTimer) {
        clearTimeout(state.allTaxiRefreshTimer);
        state.allTaxiRefreshTimer = null;
    }
    if (state.allTaxiBoundsRetryTimer) {
        clearTimeout(state.allTaxiBoundsRetryTimer);
        state.allTaxiBoundsRetryTimer = null;
    }
    state.currentAllTaxiPointCount = 0;
    if (clearOverlay) {
        clearTrajectoryOverlays();
    }
}

async function runAllTaxiQuery(enableMode = true) {
    const requestSeq = ++state.allTaxiRequestSeq;
    const bounds = readValidMapBounds();
    if (!bounds) {
        if (state.allTaxiBoundsRetryTimer) {
            clearTimeout(state.allTaxiBoundsRetryTimer);
        }
        state.allTaxiBoundsRetryTimer = setTimeout(() => {
            state.allTaxiBoundsRetryTimer = null;
            if (state.allTaxiMode) {
                runAllTaxiQuery(enableMode).catch((error) => {
                    renderInfoPanel("trajectory-info", [], error.message);
                });
            }
        }, 180);
        return;
    }

    const data = await requestJson("/api/trajectory", {
        method: "POST",
        body: JSON.stringify({
            taxiId: 0,
            minLon: bounds.minLon,
            minLat: bounds.minLat,
            maxLon: bounds.maxLon,
            maxLat: bounds.maxLat,
            zoom: state.map.getZoom()
        })
    });

    if (requestSeq !== state.allTaxiRequestSeq) {
        return;
    }

    if (enableMode) {
        state.allTaxiMode = true;
    }

    renderAllTaxiTrajectoryResult(data, enableMode);
}

async function runTrajectoryQuery() {
    stopAllTaxiMode(true);
    const taxiInput = qs("trajectory-id");
    if (!taxiInput) {
        throw new Error("轨迹查询输入框未找到");
    }
    const taxiIdText = taxiInput.value.trim();
    if (!taxiIdText) {
        throw new Error("请输入出租车 ID");
    }

    const taxiId = Number(taxiIdText);
    if (!Number.isFinite(taxiId) || taxiId < 0) {
        throw new Error("请输入有效的出租车 ID");
    }

    if (taxiId === 0) {
        await runAllTaxiQuery(true);
        return;
    }

    const data = await requestJson("/api/trajectory", {
        method: "POST",
        body: JSON.stringify({ taxiId })
    });

    renderSingleTrajectoryResult(data);
}

export {
    scheduleAllTaxiRefresh,
    stopAllTaxiMode,
    runAllTaxiQuery,
    runTrajectoryQuery
};
