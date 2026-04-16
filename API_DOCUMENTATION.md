# Taxi Analysis 接口文档

<<<<<<< ours
本文档按当前代码库中的 `src/httpserver.cpp` 实际实现整理，目的是让你快速判断哪些接口后续需要改造。
=======
<<<<<<< ours
本文档按当前代码库中的 `src/httpserver.cpp` 实际实现整理，目的是让你快速判断哪些接口后续需要改造。
=======
最后更新：2026-04-16  
依据代码：`src/httpserver.cpp`
>>>>>>> theirs
>>>>>>> theirs

## 1. 基础约定

### 1.1 服务地址
<<<<<<< ours
=======
<<<<<<< ours
>>>>>>> theirs
- 本地调试默认地址：`http://127.0.0.1:8080`

### 1.2 通信协议
- 业务控制请求：JSON
- 业务错误响应：JSON
- 当前代码中的分析结果接口：仍是 JSON

### 1.3 通用响应格式
成功：

<<<<<<< ours
=======
=======
- 本地默认：`http://127.0.0.1:8080`

### 1.2 协议与返回格式
- 业务请求：JSON
- 业务响应：JSON

成功响应：
>>>>>>> theirs
>>>>>>> theirs
```json
{
  "success": true,
  "data": {}
}
```

<<<<<<< ours
失败：

=======
<<<<<<< ours
失败：

=======
失败响应：
>>>>>>> theirs
>>>>>>> theirs
```json
{
  "success": false,
  "error": {
    "code": "INVALID_ARGUMENT",
    "message": "error detail"
  }
}
```

<<<<<<< ours
=======
<<<<<<< ours
>>>>>>> theirs
### 1.4 通用错误码
- `INVALID_JSON`：请求体不是合法 JSON
- `INVALID_ARGUMENT`：参数缺失或非法
- `ANALYSIS_FAILED`：密度分析执行失败
- `NOT_FOUND`：查询缓存不存在或已过期
- `FILE_ERROR`：静态文件读取失败

### 1.5 CORS
- 后端允许跨域
- 允许方法：`GET, POST, OPTIONS`

---

## 2. 接口总览

| 模块 | 方法 | 路径 | 说明 |
|---|---|---|---|
| 静态页面 | GET | `/` | 返回前端首页 |
| 预检 | OPTIONS | `/*` | 跨域预检 |
| 基础信息 | GET | `/api/health` | 服务健康检查 |
| 基础信息 | GET | `/api/meta` | 地图与全局配置 |
| 轨迹查询 | POST | `/api/trajectory` | 单车轨迹或视野内全部车辆 |
| 区域查询 | POST | `/api/region-search` | 区域 + 时间范围统计 |
| 密度分析 | POST | `/api/density/meta` | 密度分析元信息与查询缓存入口 |
| 密度分析 | POST | `/api/density/bucket` | 获取某个时间桶的数据 |
| 密度分析 | POST | `/api/density/cell-trend` | 获取某个网格的时间趋势 |

---

## 3. 静态页面

### 3.1 首页
- 请求方法：`GET`
- 请求路径：`/`
- 返回内容：`index.html`

如果 `index.html` 不存在：
- 返回 `404`
- 响应体为 JSON 错误

如果文件无法读取：
- 返回 `500`
- 响应体为 JSON 错误

---

## 4. 基础信息接口

### 4.1 服务健康检查
- 请求方法：`GET`
- 请求路径：`/api/health`

响应字段：
- `data.status`：固定为 `ok`
- `data.pointsLoaded`：当前已加载的点数

示例：

<<<<<<< ours
=======
=======
### 1.3 通用错误码
- `INVALID_JSON`: 请求体不是合法 JSON 对象
- `INVALID_ARGUMENT`: 参数缺失或非法
- `ANALYSIS_FAILED`: 密度分析执行失败
- `NOT_FOUND`: 资源不存在（常见于密度缓存过期）
- `FILE_ERROR`: 静态文件读取失败

### 1.4 CORS
- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Headers: Content-Type`
- `Access-Control-Allow-Methods: GET, POST, OPTIONS`

---

## 2. 路由总览

| 方法 | 路径 | 说明 |
|---|---|---|
| OPTIONS | `/*` | CORS 预检 |
| GET | `/` | 返回前端 `index.html` |
| GET | `/api/health` | 健康检查 |
| GET | `/api/meta` | 地图与数据基础信息 |
| POST | `/api/trajectory` | 单车轨迹或全量车辆视野查询 |
| POST | `/api/region-search` | 区域 + 时间范围统计 |
| POST | `/api/density/meta` | 发起/复用密度分析，返回元信息 |
| POST | `/api/density/bucket` | 按时间桶取密度网格数据 |
| POST | `/api/density/cell-trend` | 按网格取时序趋势 |

---

## 3. 基础接口

### 3.1 GET `/api/health`

响应字段：
- `status`: 固定为 `ok`
- `pointsLoaded`: 已加载点数量

示例：
>>>>>>> theirs
>>>>>>> theirs
```json
{
  "success": true,
  "data": {
    "status": "ok",
    "pointsLoaded": 893530
  }
}
```

<<<<<<< ours
=======
<<<<<<< ours
>>>>>>> theirs
### 4.2 地图与全局配置
- 请求方法：`GET`
- 请求路径：`/api/meta`

响应字段：
- `minLon / maxLon / minLat / maxLat`：全局地图边界
- `centerLon / centerLat`：地图中心点
- `initialZoom`：初始缩放级别
- `minZoom / maxZoom`：缩放范围
- `totalPoints`：已加载点数
- `baiduMapAk`：百度地图 AK

示例：

```json
{
  "success": true,
  "data": {
    "minLon": 115,
    "maxLon": 118,
    "minLat": 39,
    "maxLat": 41,
    "centerLon": 116.404,
    "centerLat": 39.915,
    "initialZoom": 12,
    "minZoom": 8,
    "maxZoom": 18,
    "totalPoints": 893530,
    "baiduMapAk": "xxxx"
  }
}
```

---

## 5. 轨迹查询接口

### 5.1 单车轨迹 / 全车视野查询
- 请求方法：`POST`
- 请求路径：`/api/trajectory`

#### 请求体

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `taxiId` | number | 是 | 车辆 ID，`0` 表示全车视野模式 |
| `minLon` | number | 条件必填 | `taxiId=0` 时必填 |
| `minLat` | number | 条件必填 | `taxiId=0` 时必填 |
| `maxLon` | number | 条件必填 | `taxiId=0` 时必填 |
| `maxLat` | number | 条件必填 | `taxiId=0` 时必填 |
| `zoom` | number | 条件必填 | `taxiId=0` 时影响 raw/cluster 返回 |

#### 返回模式

- `taxiId > 0`：
  - `data.mode = "trajectory"`
  - 返回该出租车的完整轨迹点

- `taxiId = 0`：
  - 如果缩放足够大且点数不多，返回 `raw`
  - 否则返回 `cluster`

#### 单车返回字段
- `data.taxiId`
- `data.mode`
- `data.pointCount`
- `data.points`

#### 全车视野返回字段
- `data.taxiId`
- `data.mode`
- `data.pointCount`
- `data.clusterCount`
- `data.renderCap`
- `data.points`

其中：
- `raw` 模式下，`points` 是原始点数组
- `cluster` 模式下，`points` 是聚合点数组

#### 点对象

```json
{
  "id": 1234,
  "timestamp": 1202190000,
  "lon": 116.4,
  "lat": 39.9
}
```

#### 失败场景
- `taxiId < 0`
- `taxiId = 0` 但缺少边界参数
- 边界非法，`min >= max`

---

## 6. 区域查询接口

### 6.1 区域 + 时间统计
- 请求方法：`POST`
- 请求路径：`/api/region-search`

#### 请求体

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `minLon` | number | 是 | 区域最小经度 |
| `minLat` | number | 是 | 区域最小纬度 |
| `maxLon` | number | 是 | 区域最大经度 |
| `maxLat` | number | 是 | 区域最大纬度 |
| `startTime` | number/string | 是 | 开始时间 |
| `endTime` | number/string | 是 | 结束时间 |

#### 返回字段
- `data.pointCount`：命中点数
- `data.vehicleCount`：去重车辆数
- `data.elapsedSeconds`：查询耗时

#### 示例

```json
{
  "success": true,
  "data": {
    "pointCount": 5321,
    "vehicleCount": 184,
    "elapsedSeconds": 0.042
  }
}
```

#### 失败场景
- 边界非法，`min >= max`
- 时间范围非法，`startTime > endTime`

---

## 7. 密度分析接口

当前实现把密度分析拆成 3 个接口：
- `meta`：发起分析，拿查询元信息
- `bucket`：按时间桶取细粒度数据
- `cell-trend`：按网格取时间趋势

这三个接口当前都还是 JSON，不是 Arrow IPC。

### 7.1 密度分析元信息
- 请求方法：`POST`
- 请求路径：`/api/density/meta`

#### 请求体

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `startTime` | number/string | 是 | 开始时间 |
| `endTime` | number/string | 是 | 结束时间 |
| `intervalMinutes` | number | 否 | 时间桶粒度，默认 `30` |
| `cellSizeMeters` | number | 否 | 网格边长，默认 `500` |
| `minLon` | number | 条件必填 | 若传区域字段，必须四个一起传 |
| `minLat` | number | 条件必填 | 同上 |
| `maxLon` | number | 条件必填 | 同上 |
| `maxLat` | number | 条件必填 | 同上 |

#### 规则
- 如果完全不传区域字段，则回退到全图配置范围
- 如果只传了部分区域字段，会报错
- 如果传了完整区域字段，则以该区域为准

#### 返回字段
- `queryId`：本次分析的缓存 ID
- `regionSource`：`selection` 或 `full-map`
- `startTime / endTime`
- `intervalMinutes`
- `bucketSeconds`
- `cellSizeMeters`
- `minLon / minLat / maxLon / maxLat`
- `lonStep / latStep`
- `cellAreaKm2`
- `columnCount / rowCount`
- `bucketCount`
- `gridCount`
- `analysisScale`
- `maxVehicleDensity`
- `totalPointCount`
- `totalVehicleCount`
- `elapsedSeconds`
- `cacheFetchCostMs`
- `buckets`：每个桶的摘要

#### `buckets` 摘要字段
- `startTime`
- `endTime`
- `nonZeroCount`
- `maxDensity`

#### 示例

```json
{
  "success": true,
  "data": {
    "queryId": "density_123",
    "regionSource": "full-map",
    "startTime": 1202190000,
    "endTime": 1202211600,
    "intervalMinutes": 30,
    "bucketSeconds": 1800,
    "cellSizeMeters": 500,
    "minLon": 115,
    "minLat": 39,
    "maxLon": 118,
    "maxLat": 41,
    "lonStep": 0.0051,
    "latStep": 0.0045,
    "cellAreaKm2": 0.25,
    "columnCount": 42,
    "rowCount": 36,
    "bucketCount": 48,
    "gridCount": 1512,
    "analysisScale": 72576,
    "maxVehicleDensity": 284.0,
    "totalPointCount": 13821,
    "totalVehicleCount": 10375,
    "elapsedSeconds": 0.183,
    "cacheFetchCostMs": 2,
    "buckets": [
      {
        "startTime": 1202190000,
        "endTime": 1202191799,
        "nonZeroCount": 612,
        "maxDensity": 284.0
      }
    ]
  }
}
```

### 7.2 按桶取数据
- 请求方法：`POST`
- 请求路径：`/api/density/bucket`

#### 请求体

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `queryId` | string | 是 | `meta` 返回的查询 ID |
| `bucketIndex` | number | 是 | 时间桶索引，从 `0` 开始 |

#### 返回字段
- `queryId`
- `bucketIndex`
- `startTime`
- `endTime`
- `nonZeroCount`
- `bucketSeconds`
- `cells`

#### `cells` 格式
- 每个元素是一个三元组：`[gx, gy, seconds]`
- 只返回有值的格子

#### 示例

```json
{
  "success": true,
  "data": {
    "queryId": "density_123",
    "bucketIndex": 0,
    "startTime": 1202190000,
    "endTime": 1202191799,
    "nonZeroCount": 612,
    "bucketSeconds": 1800,
    "cells": [
      [10, 8, 24.5],
      [11, 8, 13.0]
    ]
  }
}
```

#### 失败场景
- `queryId` 缺失
- `bucketIndex` 非整数
- 查询缓存不存在或已过期
- `bucketIndex` 越界

### 7.3 网格时间趋势
- 请求方法：`POST`
- 请求路径：`/api/density/cell-trend`

#### 请求体

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `queryId` | string | 是 | `meta` 返回的查询 ID |
| `gx` | number | 是 | 网格列索引 |
| `gy` | number | 是 | 网格行索引 |

#### 返回字段
- `queryId`
- `gx`
- `gy`
- `series`

#### `series` 格式
- 每个元素是一个四元组：`[bucketIndex, startTime, endTime, seconds]`
- 代表该网格在每个时间桶里的累计停留秒数

#### 示例

```json
{
  "success": true,
  "data": {
    "queryId": "density_123",
    "gx": 10,
    "gy": 8,
    "series": [
      [0, 1202190000, 1202191799, 24.5],
      [1, 1202191800, 1202193599, 30.0]
    ]
  }
}
```

#### 失败场景
- `queryId` 缺失
- `gx / gy` 非整数
- 查询缓存不存在或已过期
- `gx / gy` 越界

---

## 8. 当前接口的实现特征

### 8.1 轨迹查询
- 仍使用 JSON 返回
- 全车视野模式已经支持 raw / cluster 两种返回方式

### 8.2 区域查询
- 仍使用 JSON 返回
- 当前实现基于空间查询和时间筛选

### 8.3 密度分析
- 当前拆成 `meta / bucket / cell-trend`
- 仍使用 JSON 返回
- 后端带缓存，`queryId` 会在后续请求中复用
- 单次返回的数据量明显比旧版更小，但仍属于数值密集型接口

---

## 9. 已废弃或不再匹配当前代码的旧说明

以下内容不再对应当前实现：
- 单一的 `/api/density` 全量接口
- 旧版一次性返回完整 `buckets[].cells[]` 的密度响应
- 旧版 `DensityGridCell / DensityTimeBucket` 作为直接对外接口结构的说明

如果你后续要改协议，优先改的是：
- `/api/density/meta`
- `/api/density/bucket`
- `/api/density/cell-trend`

<<<<<<< ours
=======
=======
### 3.2 GET `/api/meta`

响应字段：
- `minLon`, `maxLon`, `minLat`, `maxLat`
- `centerLon`, `centerLat`
- `initialZoom`, `minZoom`, `maxZoom`
- `totalPoints`
- `baiduMapAk`

---

## 4. 轨迹查询

### 4.1 POST `/api/trajectory`

请求体（通用）：
- `taxiId` (number, required)

#### 场景 A：`taxiId > 0`（单车轨迹）

请求体：
```json
{
  "taxiId": 123
}
```

响应：
- `mode = "trajectory"`
- `points`: 轨迹点数组（`id`, `timestamp`, `lon`, `lat`）

#### 场景 B：`taxiId = 0`（全量车辆视野）

额外必填：
- `minLon`, `minLat`, `maxLon`, `maxLat`
- 建议传 `zoom`（不传时后端默认 12）

请求体：
```json
{
  "taxiId": 0,
  "minLon": 116.2,
  "minLat": 39.8,
  "maxLon": 116.6,
  "maxLat": 40.0,
  "zoom": 16
}
```

响应模式：
- 当 `zoom >= 18` 且点数 `<= 12000`：`mode = "raw"`
- 否则：`mode = "cluster"`

`cluster` 模式点字段：
- `lng`, `lat`, `count`, `isCluster`
- `minLon`, `minLat`, `maxLon`, `maxLat`

常见错误：
- `taxiId < 0`
- `taxiId = 0` 但缺少边界参数
- 地图边界非法（`min >= max`）

---

## 5. 区域查询

### 5.1 POST `/api/region-search`

请求体：
- `minLon`, `minLat`, `maxLon`, `maxLat`（required）
- `startTime`, `endTime`（required，number 或 string，后端转为 int64）

示例：
```json
{
  "minLon": 116.2,
  "minLat": 39.8,
  "maxLon": 116.6,
  "maxLat": 40.0,
  "startTime": 1202190000,
  "endTime": 1202211600
}
```

响应字段：
- `pointCount`
- `vehicleCount`
- `elapsedSeconds`

常见错误：
- 区域非法（`min >= max`）
- 时间范围非法（`startTime > endTime`）

---

## 6. 密度分析接口（3 段式）

当前密度分析为 3 段式：
1. `meta`：创建或复用分析缓存，返回 `queryId`
2. `bucket`：按时间桶拉网格数据
3. `cell-trend`：按网格拉完整时间序列

### 6.1 POST `/api/density/meta`

请求体：
- `startTime`, `endTime`（required）
- `intervalMinutes`（optional，默认 30）
- `cellSizeMeters`（optional，默认 500）
- 可选区域：`minLon`, `minLat`, `maxLon`, `maxLat`

区域参数规则：
- 四个边界字段必须同时提供才会生效
- 如果只传部分字段，返回 `INVALID_ARGUMENT`
- 不传区域字段时，使用全图范围（配置中的 map bounds）

响应关键字段：
- `queryId`
- `regionSource`：`selection` 或 `full-map`
- `bucketSeconds`, `cellAreaKm2`
- `columnCount`, `rowCount`, `bucketCount`, `gridCount`
- `maxVehicleDensity`
- `totalPointCount`, `totalVehicleCount`
- `elapsedSeconds`, `cacheFetchCostMs`
- `buckets`: 每个时间桶摘要（`startTime`, `endTime`, `nonZeroCount`, `maxDensity`）

### 6.2 POST `/api/density/bucket`

请求体：
- `queryId` (string, required)
- `bucketIndex` (int, required)

响应字段：
- `queryId`, `bucketIndex`, `startTime`, `endTime`
- `nonZeroCount`, `bucketSeconds`
- `cells`: 数组，每项为 `[gx, gy, seconds]`，仅返回 `seconds > 0` 的网格

常见错误：
- 缺少 `queryId`
- `bucketIndex` 非整数或越界
- `queryId` 对应缓存不存在或已过期（`NOT_FOUND`）

### 6.3 POST `/api/density/cell-trend`

请求体：
- `queryId` (string, required)
- `gx` (int, required)
- `gy` (int, required)

响应字段：
- `queryId`, `gx`, `gy`
- `series`: 数组，每项为 `[bucketIndex, startTime, endTime, seconds]`

说明：
- `series` 会覆盖全部时间桶（即使某些桶 `seconds=0` 也会返回）

---

## 7. 静态资源

### 7.1 GET `/`
- 返回 `web/index.html`
- 若文件不存在：`404 + NOT_FOUND`
- 若读取失败：`500 + FILE_ERROR`

### 7.2 静态挂载
- 服务端通过 `set_mount_point("/", webRoot)` 挂载 `web` 目录
- 可直接访问前端静态资源（如 `/css/style.css`, `/js/app.js`）

---

## 8. 与旧版差异（已废弃）

以下接口/行为不再使用：
- 单接口全量密度：`POST /api/density`
- 一次性返回所有桶所有网格的旧密度结构

前端应始终使用：
- `/api/density/meta`
- `/api/density/bucket`
- `/api/density/cell-trend`
>>>>>>> theirs
>>>>>>> theirs
