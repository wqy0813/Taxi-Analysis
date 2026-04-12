# Taxi Analysis 接口文档

## 1. 文档目的
本文档用于前后端开发、测试与联调，说明当前项目已实现接口的请求方式、参数约束、响应结构和错误返回。

## 2. 适用范围
适用于当前仓库代码中由 `httpserver.cpp` 提供的全部 HTTP 接口与前端联调行为。

## 3. 认证方式
当前版本无登录态、无 Token、无会话鉴权。所有接口默认开放。

## 4. 通用说明

### 4.1 基础地址
- 本地：`http://127.0.0.1:8080`

### 4.2 通用请求头
- `Content-Type: application/json`（POST 接口）
- `Accept: application/json`

### 4.3 通用响应结构
成功：

```json
{
  "success": true,
  "data": {}
}
```

失败：

```json
{
  "success": false,
  "error": {
    "code": "INVALID_ARGUMENT",
    "message": "error detail"
  }
}
```

### 4.4 通用错误码
- `INVALID_JSON`：请求体不是合法 JSON 对象
- `INVALID_ARGUMENT`：参数不合法
- `ANALYSIS_FAILED`：分析执行失败
- `CONFIG_ERROR`：配置错误（如地图边界配置异常）
- `NOT_FOUND`：资源不存在
- `FILE_ERROR`：文件读取失败

### 4.5 HTTP 状态码
- `200`：成功
- `204`：预检成功（OPTIONS）
- `400`：请求参数或请求体错误
- `404`：资源不存在
- `500`：服务端配置或文件错误

---

## 5. 接口总览

| 模块 | 接口 | 方法 | 路径 |
|---|---|---|---|
| 静态页面 | 首页 | GET | `/` |
| 跨域预检 | 预检 | OPTIONS | `/*` |
| 基础信息 | 服务状态 | GET | `/api/health` |
| 基础信息 | 地图配置 | GET | `/api/meta` |
| 轨迹查询 | 轨迹查询 | POST | `/api/trajectory` |
| 区域查询 | 区域查询 | POST | `/api/region-search` |
| 密度分析 | 区域车流密度分析 | POST | `/api/density` |

---

## 6. 基础信息接口

### 6.1 服务状态
- 接口用途：确认服务是否可用
- 请求方法：`GET`
- 请求路径：`/api/health`
- 是否需要登录：否

请求示例：

```http
GET /api/health HTTP/1.1
Host: 127.0.0.1:8080
```

响应示例：

```json
{
  "success": true,
  "data": {
    "status": "ok",
    "pointsLoaded": 893530
  }
}
```

响应字段说明：
- `data.status`：固定 `ok`
- `data.pointsLoaded`：当前已加载点数

---

### 6.2 地图配置
- 接口用途：前端初始化地图参数与基础范围
- 请求方法：`GET`
- 请求路径：`/api/meta`
- 是否需要登录：否

响应示例：

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

## 7. 轨迹查询接口

### 7.1 轨迹查询
- 接口用途：
  - `taxiId > 0`：查询单车完整轨迹
  - `taxiId = 0`：查询当前视野内全部车辆点（可能返回聚合结果）
- 请求方法：`POST`
- 请求路径：`/api/trajectory`
- 是否需要登录：否

#### Body 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `taxiId` | number | 是 | 车辆 ID，`0` 表示全部车辆模式 |
| `minLon` | number | 条件必填 | 仅 `taxiId=0` 时必填 |
| `minLat` | number | 条件必填 | 仅 `taxiId=0` 时必填 |
| `maxLon` | number | 条件必填 | 仅 `taxiId=0` 时必填 |
| `maxLat` | number | 条件必填 | 仅 `taxiId=0` 时必填 |
| `zoom` | number | 条件必填 | 仅 `taxiId=0` 时用于控制 raw/cluster 返回 |

#### 请求示例（单车）

```json
{
  "taxiId": 1234
}
```

#### 请求示例（全部车辆）

```json
{
  "taxiId": 0,
  "minLon": 116.1,
  "minLat": 39.8,
  "maxLon": 116.6,
  "maxLat": 40.1,
  "zoom": 12
}
```

#### 响应字段（核心）
- `data.mode`：
  - `trajectory`：单车轨迹
  - `raw`：全部车辆原始点
  - `cluster`：全部车辆聚合点
- `data.points`：对应模式下的点集合

#### 错误场景
- `taxiId < 0`
- `taxiId=0` 但缺少视野边界
- 视野边界不合法（`min >= max`）

---

## 8. 区域查询接口

### 8.1 区域查询
- 接口用途：按区域 + 时间范围统计车辆数与采样点数
- 请求方法：`POST`
- 请求路径：`/api/region-search`
- 是否需要登录：否

#### Body 参数

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `minLon` | number | 是 | 最小经度 |
| `minLat` | number | 是 | 最小纬度 |
| `maxLon` | number | 是 | 最大经度 |
| `maxLat` | number | 是 | 最大纬度 |
| `startTime` | number/string | 是 | 开始时间（秒级时间戳） |
| `endTime` | number/string | 是 | 结束时间（秒级时间戳） |

#### 请求示例

```json
{
  "minLon": 116.1,
  "minLat": 39.8,
  "maxLon": 116.6,
  "maxLat": 40.1,
  "startTime": 1202190000,
  "endTime": 1202211600
}
```

#### 响应示例

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

---

## 9. 区域车流密度分析接口

### 9.1 密度分析
- 接口用途：按“空间网格 + 时间分桶”统计车流密度变化
- 请求方法：`POST`
- 请求路径：`/api/density`
- 是否需要登录：否
- 区域规则：
  - 若请求体传入完整 `minLon/minLat/maxLon/maxLat`，优先使用该区域（框选区域）
  - 若未传区域参数，回退到全图配置范围
  - 若只传了部分区域字段，直接报错

#### Body 参数

| 参数 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `startTime` | number/string | 是 | 无 | 开始时间（秒级时间戳） |
| `endTime` | number/string | 是 | 无 | 结束时间（秒级时间戳） |
| `intervalMinutes` | number | 否 | 30 | 时间分桶粒度（分钟） |
| `cellSizeMeters` | number | 否 | 500 | 网格边长（米） |
| `minLon` | number | 否 | 回退全图 | 区域最小经度（四字段需完整传入） |
| `minLat` | number | 否 | 回退全图 | 区域最小纬度（四字段需完整传入） |
| `maxLon` | number | 否 | 回退全图 | 区域最大经度（四字段需完整传入） |
| `maxLat` | number | 否 | 回退全图 | 区域最大纬度（四字段需完整传入） |

#### 请求示例（使用框选区域）

```json
{
  "startTime": 1202190000,
  "endTime": 1202211600,
  "intervalMinutes": 30,
  "cellSizeMeters": 500,
  "minLon": 116.2,
  "minLat": 39.85,
  "maxLon": 116.5,
  "maxLat": 40.05
}
```

#### 请求示例（回退全图）

```json
{
  "startTime": 1202190000,
  "endTime": 1202211600,
  "intervalMinutes": 30,
  "cellSizeMeters": 500
}
```

#### 响应示例（节选）

```json
{
  "success": true,
  "data": {
    "minLon": 116.2,
    "minLat": 39.85,
    "maxLon": 116.5,
    "maxLat": 40.05,
    "regionSource": "selection",
    "totalPointCount": 13821,
    "totalVehicleCount": 10375,
    "elapsedSeconds": 0.183,
    "lonStep": 0.0051,
    "latStep": 0.0045,
    "cellAreaKm2": 0.25,
    "columnCount": 42,
    "rowCount": 36,
    "bucketCount": 48,
    "gridCount": 1512,
    "analysisScale": 72576,
    "maxVehicleDensity": 284.0,
    "buckets": [
      {
        "startTime": 1202190000,
        "endTime": 1202191799,
        "maxVehicleCount": 71,
        "maxVehicleDensity": 284.0,
        "avgVehicleDensity": 32.1,
        "totalPointCount": 1200,
        "totalVehicleCount": 540,
        "totalFlowDensity": 2112.4,
        "deltaRate": 0.12,
        "cells": [
          {
            "gx": 247,
            "gy": 205,
            "minLon": 116.31,
            "minLat": 39.91,
            "maxLon": 116.3151,
            "maxLat": 39.9145,
            "pointCount": 98,
            "vehicleCount": 71,
            "vehicleDensity": 284.0,
            "flowIntensity": 392.0,
            "deltaVehicleCount": 7,
            "deltaVehicleDensity": 28.0,
            "deltaRate": 0.109
          }
        ]
      }
    ]
  }
}
```

#### 响应字段说明（核心）

| 字段 | 说明 |
|---|---|
| `regionSource` | `selection`（来自前端区域）或 `full-map`（回退全图） |
| `bucketCount` | 时间桶数量 |
| `gridCount` | 网格总数（列数 × 行数） |
| `analysisScale` | 分析规模（桶数 × 网格数） |
| `buckets[].cells[].gx/gy` | 网格索引 |
| `buckets[].cells[].minLon/minLat/maxLon/maxLat` | 网格地理边界 |
| `buckets[].cells[].pointCount` | 当前桶该网格采样点数 |
| `buckets[].cells[].vehicleCount` | 当前桶该网格去重车辆数 |
| `buckets[].cells[].vehicleDensity` | 车辆密度（车辆数/km²） |
| `buckets[].cells[].flowIntensity` | 流强度（点数/km²） |
| `buckets[].cells[].delta*` | 相对上一时间桶同网格变化 |

#### 错误场景
- 区域字段只传部分（如只传 `minLon`）
- `startTime > endTime`
- `intervalMinutes <= 0`
- `cellSizeMeters <= 0`
- 配置边界异常（回退全图场景）

---

## 10. 前端联调要点

### 10.1 初始化顺序
1. 调用 `GET /api/meta`
2. 加载地图脚本
3. 初始化地图
4. 绑定功能按钮事件

### 10.2 模块与接口对应
- 查询轨迹：`POST /api/trajectory`
- 区域查找：`POST /api/region-search`
- 车辆密度分析：`POST /api/density`

### 10.3 本地联调
- 通过 `file://` 打开前端时，请求默认发往 `http://127.0.0.1:8080`
- 通过服务端打开 `/` 时，使用同源请求
