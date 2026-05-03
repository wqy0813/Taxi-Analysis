const state = {
    meta: null,
    map: null,
    region: null,
    regionPolygon: null,
    selectingRegion: false,
    selectionStartPixel: null,
    selectionEndPixel: null,

    currentBucketIndex: 0,
    densityOverlay: null,
    densityRenderModel: null,
    densityMeta: null,
    densityQueryId: null,
    densityBucketCache: new Map(),
    densityCellMaps: new Map(),
    densityChunkMaps: new Map(),
    densityTrendCache: new Map(),
    densityQueryParams: null,
    densityChunkCache: new Map(),
    densityChunkAccessTick: 0,
    densityPlayTimer: null,
    densityHoverCell: null,
    densitySelectedCellKey: null,
    densityTrendPreviewChart: null,
    densityTrendPreviewEl: null,
    densityTrendModalChart: null,
    densityTrendModalEl: null,
    densityTrendModal: null,
    densityTrendModalTitle: null,
    densityTrendModalSubtitle: null,
    densityTrendModalClose: null,
    densityTrendModalOpen: false,
    densityTrendUiBound: false,
    densityTooltip: null,
    densityRedrawFrame: null,
    trajectoryOverlays: [],
    allTaxiDotOverlay: null,
    allTaxiPointCollection: null,
    allTaxiMode: false,
    allTaxiRequestSeq: 0,
    allTaxiRefreshTimer: null,
    allTaxiBoundsRetryTimer: null,
    currentAllTaxiPointCount: 0,
    currentAllTaxiRenderMode: "cluster",
    selectionLayer: null,
    activeDockPanel: null,
    openDockPanel: null,

    regionFlow: {
        selecting: false,
        selectingTarget: null,
        selectionLayer: null,
        selectionStartPixel: null,
        selectionEndPixel: null,
        selectionRestoreRegion: null,
        regionA: null,
        regionB: null,
        polygonA: null,
        polygonB: null,
        draggingTarget: null,
        dragStartPoint: null,
        dragOriginRegion: null,
        chart: null,
        modal: null,
        modalChart: null,
        modalTitle: null,
        modalSubtitle: null,
        modalClose: null,
        modalOpen: false,
        lastResult: null
    },

    singleRegionFlow: {
        selecting: false,
        selectionLayer: null,
        selectionStartPixel: null,
        selectionEndPixel: null,
        region: null,
        polygon: null,
        dragging: false,
        dragStartPoint: null,
        dragOriginRegion: null,
        chart: null,
        modal: null,
        modalChart: null,
        modalTitle: null,
        modalSubtitle: null,
        modalClose: null,
        modalOpen: false,
        lastResult: null
    }
};
const DENSITY_CHUNK_CELL_SIZE = 16;
const DENSITY_CHUNK_CACHE_LIMIT = 96;
const DENSITY_CHUNK_PREFETCH_MARGIN = 1;

const API_BASE = location.protocol === "file:" ? "http://127.0.0.1:8080" : "";

const REGION_MAP_INTERACTIONS = [
    ["disableDragging", "enableDragging"],
    ["disableScrollWheelZoom", "enableScrollWheelZoom"],
    ["disableDoubleClickZoom", "enableDoubleClickZoom"],
    ["disableKeyboard", "enableKeyboard"]
];

export {
    state,
    API_BASE,
    REGION_MAP_INTERACTIONS,
    DENSITY_CHUNK_CELL_SIZE,
    DENSITY_CHUNK_CACHE_LIMIT,
    DENSITY_CHUNK_PREFETCH_MARGIN
};
