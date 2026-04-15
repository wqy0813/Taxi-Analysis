import { state } from "../../core/state.js";
import { qs, renderInfoPanel, formatCount, formatDateTime, ensureRegion, ensureTimeRange, requestJson, updateModeStatus } from "../../core/utils.js";
import { clearTrajectoryOverlays, addTrajectoryOverlay, clearRegionOverlay, pointToOverlayPixel } from "../../map/map.js";
import { resetDensityState, getDensityGridModel } from "../density/densityStore.js";
import { TaxiDotOverlay } from "../../map/overlays/taxiOverlay.js";
import { stopAllTaxiMode, runAllTaxiQuery } from "./trajectoryService.js";
import { renderRegion } from "../region/regionView.js";

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

function renderSingleTrajectoryResult(data) {
    clearTrajectoryOverlays();
    resetDensityState();
    clearRegionOverlay();

    const points = Array.isArray(data?.points) ? data.points : [];
    const path = points.map((point) => new BMap.Point(point.lon, point.lat));
    if (path.length > 0) {
        const polyline = new BMap.Polyline(path, {
            strokeColor: "#13b4b0",
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
        ["起点", path.length > 0 ? `${path[0].lng.toFixed(5)}, ${path[0].lat.toFixed(5)}` : "-"],
        ["终点", path.length > 0 ? `${path[path.length - 1].lng.toFixed(5)}, ${path[path.length - 1].lat.toFixed(5)}` : "-"],
        ["用时", `${Number(data.elapsedSeconds).toFixed(3)} s`]
    ]);

    updateModeStatus("轨迹");
}

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

function createPointCollection(points) {
    if (!window.BMapLib || !window.BMapLib.PointCollection) {
        throw new Error("PointCollection 组件未加载");
    }

    const pointCollection = new BMapLib.PointCollection(points, {
        size: BMAP_POINT_SIZE_TINY,
        shape: BMAP_POINT_SHAPE_CIRCLE,
        color: "#1f78ff"
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
    const label = new BMap.Label(String(cluster.count), {
        position: new BMap.Point(cluster.lng, cluster.lat),
        offset: new BMap.Size(-12, -12)
    });
    label.setStyle({
        color: "#1f78ff",
        backgroundColor: "rgba(255, 255, 255, 0.96)",
        border: "1px solid #1f78ff",
        borderRadius: "0",
        padding: "0 5px",
        fontSize: "12px",
        lineHeight: "14px",
        whiteSpace: "nowrap",
        boxShadow: "0 1px 3px rgba(31, 120, 255, 0.12)"
    });
    return label;
}

function normalizeAllTaxiPoints(points, mode) {
    if (!Array.isArray(points)) {
        return [];
    }

    if (mode === "trajectory" || mode === "raw") {
        return points
            .map((point) => ({
                lng: Number(point.lon ?? point.lng),
                lat: Number(point.lat),
                count: 1
            }))
            .filter((point) => Number.isFinite(point.lng) && Number.isFinite(point.lat));
    }

    return points
        .map((point) => ({
            lng: Number(point.lng ?? point.lon),
            lat: Number(point.lat),
            count: Number(point.count ?? 1)
        }))
        .filter((point) => Number.isFinite(point.lng) && Number.isFinite(point.lat));
}

function renderAllTaxiTrajectoryResult(data, enableMode = true) {
    if (enableMode) {
        state.allTaxiMode = true;
    }

    clearTrajectoryOverlays();
    clearRegionOverlay();
    resetDensityState();

    const mode = String(data?.mode || "cluster");
    const zoom = state.map?.getZoom?.() ?? 12;
    const normalizedPoints = normalizeAllTaxiPoints(data?.points || [], mode);
    state.currentAllTaxiPointCount = normalizedPoints.length;

    if (normalizedPoints.length === 0) {
        state.currentAllTaxiRenderMode = mode === "raw" || mode === "trajectory" ? "point" : "cluster";
        renderInfoPanel("trajectory-info", [
            ["全图点数", "0"],
            ["视野范围", "当前范围内无数据"],
            ["渲染模式", state.currentAllTaxiRenderMode === "point" ? "海量点" : "聚合"]
        ]);
        updateModeStatus("全图车辆");
        return;
    }

    if (mode === "raw" || mode === "trajectory") {
        state.currentAllTaxiRenderMode = "point";
        if (zoom >= 18 && window.BMapLib && window.BMapLib.PointCollection) {
            const points = normalizedPoints.map((point) => new BMap.Point(point.lng, point.lat));
            state.allTaxiPointCollection = createPointCollection(points);
            state.map.addOverlay(state.allTaxiPointCollection);
        } else if (zoom >= 18) {
            normalizedPoints.forEach((point) => {
                addTrajectoryOverlay(new BMap.Marker(new BMap.Point(point.lng, point.lat), {
                    icon: new BMap.Symbol(BMap_Symbol_SHAPE_POINT, {
                        scale: 0.45,
                        fillColor: "#1f78ff",
                        fillOpacity: 1,
                        strokeColor: "#ffffff",
                        strokeWeight: 1
                    })
                }));
            });
        } else {
            state.allTaxiDotOverlay = new TaxiDotOverlay(normalizedPoints);
            state.allTaxiDotOverlay.attach(state.map);
        }
        renderInfoPanel("trajectory-info", [
            ["全图点数", formatCount(data.pointCount ?? normalizedPoints.length)],
            ["当前缩放", String(zoom)],
            ["渲染模式", "海量点"],
            ["用时", `${Number(data.elapsedSeconds || 0).toFixed(3)} s`]
        ]);
        updateModeStatus("全图车辆");
        return;
    }

    state.currentAllTaxiRenderMode = "cluster";
    normalizedPoints.forEach((cluster) => {
        addTrajectoryOverlay(createClusterMarker(cluster));
    });

    renderInfoPanel("trajectory-info", [
        ["聚合点数", formatCount(data.clusterCount ?? normalizedPoints.length)],
        ["原始点数", formatCount(data.pointCount ?? normalizedPoints.length)],
        ["当前缩放", String(zoom)],
        ["渲染模式", "聚合"],
        ["用时", `${Number(data.elapsedSeconds || 0).toFixed(3)} s`]
    ]);
    updateModeStatus("全图车辆");
}

export {
    renderSingleTrajectoryResult,
    createPointCollection,
    buildClusterBuckets,
    createClusterMarker,
    normalizeAllTaxiPoints,
    renderAllTaxiTrajectoryResult
};



