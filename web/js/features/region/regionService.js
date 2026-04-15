import { state } from "../../core/state.js";
import { qs, ensureRegion, ensureTimeRange, renderInfoPanel, formatCount, formatDateTime, updateModeStatus, requestJson } from "../../core/utils.js";
import { stopAllTaxiMode } from "../trajectory/trajectoryService.js";
import { resetDensityState } from "../density/densityStore.js";
import { clearTrajectoryOverlays } from "../../map/map.js";
import { renderRegion } from "./regionView.js";

async function runRegionQuery() {
    stopAllTaxiMode(true);
    ensureRegion();
    const { startTime, endTime } = ensureTimeRange(qs("region-start").value, qs("region-end").value);

    const data = await requestJson("/api/region-search", {
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
export {
    runRegionQuery
};



