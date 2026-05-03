import argparse
import json
import math
import sqlite3
from collections import defaultdict
from pathlib import Path


EARTH_RADIUS_M = 6371000.0


def meters_to_lat_degrees(meters):
    return meters / 111320.0


def meters_to_lon_degrees(meters, latitude_degrees):
    scale = max(math.cos(math.radians(latitude_degrees)), 1e-6)
    return meters / (111320.0 * scale)


def haversine_meters(lon1, lat1, lon2, lat2):
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = (
        math.sin(dphi / 2.0) ** 2
        + math.cos(phi1) * math.cos(phi2) * math.sin(dlambda / 2.0) ** 2
    )
    return 2.0 * EARTH_RADIUS_M * math.atan2(math.sqrt(a), math.sqrt(1.0 - a))


def cell_id(lon, lat, min_lon, min_lat, lon_step, lat_step):
    gx = int(math.floor((lon - min_lon) / lon_step))
    gy = int(math.floor((lat - min_lat) / lat_step))
    return gx, gy


def cell_center(gx, gy, min_lon, min_lat, lon_step, lat_step):
    return {
        "lon": min_lon + (gx + 0.5) * lon_step,
        "lat": min_lat + (gy + 0.5) * lat_step,
    }


def path_key(cells):
    return "|".join(f"{gx},{gy}" for gx, gy in cells)


def compress_cells(cells):
    result = []
    for cell in cells:
        if not result or result[-1] != cell:
            result.append(cell)
    return result


def path_length_meters(cells, min_lon, min_lat, lon_step, lat_step):
    total = 0.0
    for a, b in zip(cells, cells[1:]):
        pa = cell_center(a[0], a[1], min_lon, min_lat, lon_step, lat_step)
        pb = cell_center(b[0], b[1], min_lon, min_lat, lon_step, lat_step)
        total += haversine_meters(pa["lon"], pa["lat"], pb["lon"], pb["lat"])
    return total


def load_bounds(conn):
    row = conn.execute(
        "SELECT MIN(lon), MIN(lat), MAX(lon), MAX(lat) FROM taxi_points"
    ).fetchone()
    if row is None or any(value is None for value in row):
        raise RuntimeError("taxi_points table is empty")
    return {
        "min_lon": float(row[0]),
        "min_lat": float(row[1]),
        "max_lon": float(row[2]),
        "max_lat": float(row[3]),
    }


def iter_vehicle_segments(rows, max_gap_seconds):
    current_id = None
    segment = []

    for taxi_id, timestamp, lon, lat in rows:
        if current_id is None:
            current_id = taxi_id

        if taxi_id != current_id:
            if segment:
                yield current_id, segment
            current_id = taxi_id
            segment = []

        if segment and timestamp - segment[-1][0] > max_gap_seconds:
            yield current_id, segment
            segment = []

        segment.append((timestamp, lon, lat))

    if current_id is not None and segment:
        yield current_id, segment


def create_output_schema(conn):
    conn.execute(
        """
        CREATE TABLE frequent_paths (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            path_key TEXT NOT NULL UNIQUE,
            points_json TEXT NOT NULL,
            frequency INTEGER NOT NULL,
            length_meters REAL NOT NULL,
            start_lon REAL NOT NULL,
            start_lat REAL NOT NULL,
            end_lon REAL NOT NULL,
            end_lat REAL NOT NULL,
            cell_count INTEGER NOT NULL
        )
        """
    )
    conn.execute(
        "CREATE INDEX idx_frequent_paths_length_freq "
        "ON frequent_paths(length_meters, frequency DESC)"
    )
    conn.execute(
        "CREATE INDEX idx_frequent_paths_start ON frequent_paths(start_lon, start_lat)"
    )
    conn.execute(
        "CREATE INDEX idx_frequent_paths_end ON frequent_paths(end_lon, end_lat)"
    )


def generate_frequent_paths(args):
    source_db = Path(args.source_db)
    output_db = Path(args.output_db)

    if not source_db.exists():
        raise FileNotFoundError(f"source database not found: {source_db}")

    source_conn = sqlite3.connect(source_db)
    bounds = load_bounds(source_conn)
    center_lat = (bounds["min_lat"] + bounds["max_lat"]) / 2.0
    lat_step = meters_to_lat_degrees(args.cell_size_meters)
    lon_step = meters_to_lon_degrees(args.cell_size_meters, center_lat)

    path_to_taxis = defaultdict(set)
    rows = source_conn.execute(
        "SELECT id, time, lon, lat FROM taxi_points ORDER BY id ASC, time ASC"
    )

    vehicle_count = 0
    for taxi_id, segment in iter_vehicle_segments(rows, args.max_gap_seconds):
        vehicle_count += 1
        raw_cells = [
            cell_id(lon, lat, bounds["min_lon"], bounds["min_lat"], lon_step, lat_step)
            for _timestamp, lon, lat in segment
        ]
        cells = compress_cells(raw_cells)
        if len(cells) < args.min_window_cells:
            continue

        seen_in_vehicle = set()
        for start in range(len(cells)):
            max_end = min(len(cells), start + args.max_window_cells)
            for end in range(start + args.min_window_cells, max_end + 1):
                window = tuple(cells[start:end])
                seen_in_vehicle.add(path_key(window))

        for key in seen_in_vehicle:
            path_to_taxis[key].add(int(taxi_id))

        if vehicle_count % 500 == 0:
            print(f"processed vehicles: {vehicle_count}, unique paths: {len(path_to_taxis)}")

    source_conn.close()

    output_db.parent.mkdir(parents=True, exist_ok=True)
    if output_db.exists():
        output_db.unlink()

    output_conn = sqlite3.connect(output_db)
    create_output_schema(output_conn)

    insert_rows = []
    for key, taxi_ids in path_to_taxis.items():
        frequency = len(taxi_ids)
        if frequency < args.min_frequency:
            continue

        cells = [
            tuple(int(part) for part in token.split(","))
            for token in key.split("|")
        ]
        length = path_length_meters(
            cells,
            bounds["min_lon"],
            bounds["min_lat"],
            lon_step,
            lat_step,
        )
        points = [
            cell_center(gx, gy, bounds["min_lon"], bounds["min_lat"], lon_step, lat_step)
            for gx, gy in cells
        ]
        insert_rows.append(
            (
                key,
                json.dumps(points, separators=(",", ":")),
                frequency,
                length,
                points[0]["lon"],
                points[0]["lat"],
                points[-1]["lon"],
                points[-1]["lat"],
                len(cells),
            )
        )

    output_conn.executemany(
        """
        INSERT INTO frequent_paths (
            path_key,
            points_json,
            frequency,
            length_meters,
            start_lon,
            start_lat,
            end_lon,
            end_lat,
            cell_count
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        insert_rows,
    )
    output_conn.commit()
    output_conn.close()

    print(f"source_db: {source_db}")
    print(f"output_db: {output_db}")
    print(f"vehicles_processed: {vehicle_count}")
    print(f"unique_paths_seen: {len(path_to_taxis)}")
    print(f"paths_stored: {len(insert_rows)}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate a SQLite database of frequent grid paths for F7/F8."
    )
    parser.add_argument("--source-db", default="processed_data/taxi_data.db")
    parser.add_argument("--output-db", default="processed_data/frequent_paths.db")
    parser.add_argument("--cell-size-meters", type=float, default=200.0)
    parser.add_argument("--min-window-cells", type=int, default=5)
    parser.add_argument("--max-window-cells", type=int, default=16)
    parser.add_argument("--max-gap-seconds", type=int, default=600)
    parser.add_argument("--min-frequency", type=int, default=2)
    args = parser.parse_args()
    generate_frequent_paths(args)


if __name__ == "__main__":
    main()
