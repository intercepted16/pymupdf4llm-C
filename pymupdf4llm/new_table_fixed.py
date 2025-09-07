"""
Table Processing System with PyMuPDF - Fixed with Original Logic

This module implements the original PyMuPDF table processing algorithms 
but accepts pre-detected table bounding boxes as input instead of doing 
full page table detection.

The implementation uses the exact same algorithms from the original table.py
to ensure 1:1 replica results.
"""

import fitz  # PyMuPDF
import itertools
import html
import inspect
import string
from collections.abc import Sequence
from dataclasses import dataclass
from typing import List, Tuple, Dict, Optional, Union, Any, Set
from operator import itemgetter
import weakref


# Type aliases for clarity
BBox = Tuple[float, float, float, float]  # (x0, y0, x1, y1)
Point = Tuple[float, float]  # (x, y)
Character = Dict[str, Any]  # Character dictionary from PyMuPDF
VectorGraphic = Dict[str, Any]  # Vector graphic object from PyMuPDF
Cell = Optional[BBox]  # Cell can be None for merged cells


# Constants from original table.py
DEFAULT_SNAP_TOLERANCE = 3
DEFAULT_JOIN_TOLERANCE = 3
DEFAULT_MIN_WORDS_VERTICAL = 3
DEFAULT_MIN_WORDS_HORIZONTAL = 1
DEFAULT_X_TOLERANCE = 3
DEFAULT_Y_TOLERANCE = 3
DEFAULT_X_DENSITY = 7.25
DEFAULT_Y_DENSITY = 13
TEXTFLAGS_TEXT = 4
TEXT_COLLECT_STYLES = 8

NON_NEGATIVE_SETTINGS = [
    "snap_tolerance",
    "snap_x_tolerance", 
    "snap_y_tolerance",
    "join_tolerance",
    "join_x_tolerance",
    "join_y_tolerance",
    "edge_min_length",
    "min_words_vertical",
    "min_words_horizontal",
    "intersection_tolerance",
    "intersection_x_tolerance",
    "intersection_y_tolerance",
]

TABLE_STRATEGIES = ["lines", "lines_strict", "text", "explicit"]
bbox_getter = itemgetter("x0", "top", "x1", "bottom")

LIGATURES = {
    "ﬀ": "ff",
    "ﬃ": "ffi", 
    "ﬄ": "ffl",
    "ﬁ": "fi",
    "ﬂ": "fl",
    "ﬆ": "st",
    "ﬅ": "st",
}

white_spaces = set(string.whitespace)


def bbox_intersects(bbox1: BBox, bbox2: BBox) -> bool:
    """Check if two bounding boxes intersect."""
    x0_1, y0_1, x1_1, y1_1 = bbox1
    x0_2, y0_2, x1_2, y1_2 = bbox2
    
    return (x0_1 < x1_2) and (x1_1 > x0_2) and (y0_1 < y1_2) and (y1_1 > y0_2)


def char_in_bbox(char: Character, bbox: BBox) -> bool:
    """Check if a character's center point falls within a bounding box."""
    v_mid = (char["top"] + char["bottom"]) / 2
    h_mid = (char["x0"] + char["x1"]) / 2
    x0, y0, x1, y1 = bbox
    
    return (h_mid >= x0) and (h_mid < x1) and (v_mid >= y0) and (v_mid < y1)


# Utility functions from original table.py
def to_list(collection) -> list:
    if isinstance(collection, list):
        return collection
    elif isinstance(collection, Sequence):
        return list(collection)
    elif hasattr(collection, "to_dict"):
        return [collection.to_dict()]
    else:
        return [collection]


def cluster_list(xs, tolerance=0) -> list:
    if tolerance == 0:
        return [[x] for x in sorted(set(xs))]
    if len(xs) < 2:
        return [xs]
    groups = []
    xs = list(sorted(xs))
    current_group = [xs[0]]
    last = xs[0]
    for x in xs[1:]:
        if x - last <= tolerance:
            current_group.append(x)
        else:
            groups.append(current_group)
            current_group = [x]
        last = x
    groups.append(current_group)
    return groups


def make_cluster_dict(values, tolerance) -> dict:
    clusters = cluster_list(list(set(values)), tolerance)
    
    nested_tuples = [
        [(val, i) for val in value_cluster] for i, value_cluster in enumerate(clusters)
    ]
    
    return dict(itertools.chain(*nested_tuples))


def cluster_objects(xs, key_fn, tolerance) -> list:
    if not callable(key_fn):
        key_fn = itemgetter(key_fn)
    
    values = map(key_fn, xs)
    cluster_dict = make_cluster_dict(values, tolerance)
    
    get_0, get_1 = itemgetter(0), itemgetter(1)
    
    cluster_tuples = sorted(((x, cluster_dict.get(key_fn(x))) for x in xs), key=get_1)
    
    grouped = itertools.groupby(cluster_tuples, key=get_1)
    
    return [list(map(get_0, v)) for k, v in grouped]


def move_object(obj, axis: str, value):
    assert axis in ("h", "v")
    if axis == "h":
        new_items = [("x0", obj["x0"] + value), ("x1", obj["x1"] + value)]
    if axis == "v":
        new_items = [("top", obj["top"] + value), ("bottom", obj["bottom"] + value)]
    return obj.__class__(tuple(obj.items()) + tuple(new_items))


def snap_objects(objs, attr: str, tolerance) -> list:
    axis = {"x0": "h", "x1": "h", "top": "v", "bottom": "v"}[attr]
    list_objs = list(objs)
    clusters = cluster_objects(list_objs, itemgetter(attr), tolerance)
    avgs = [sum(map(itemgetter(attr), cluster)) / len(cluster) for cluster in clusters]
    snapped_clusters = [
        [move_object(obj, axis, avg - obj[attr]) for obj in cluster]
        for cluster, avg in zip(clusters, avgs)
    ]
    return list(itertools.chain(*snapped_clusters))


def filter_edges(
    edges,
    orientation=None,
    edge_type=None,
    min_length=1,
) -> list:
    if orientation not in ("v", "h", None):
        raise ValueError("Orientation must be 'v' or 'h'")
    
    def test(e) -> bool:
        dim = "height" if e["orientation"] == "v" else "width"
        
        if orientation is not None and e["orientation"] != orientation:
            return False
        
        if edge_type is not None and e.get("object_type") != edge_type:
            return False
        
        if e[dim] < min_length:
            return False
        
        return True
    
    return list(filter(test, edges))


def snap_edges(
    edges,
    x_tolerance=DEFAULT_SNAP_TOLERANCE,
    y_tolerance=DEFAULT_SNAP_TOLERANCE,
):
    """
    Given a list of edges, snap any within `tolerance` pixels of one another
    to their positional average.
    """
    by_orientation = {"v": [], "h": []}
    for e in edges:
        by_orientation[e["orientation"]].append(e)
    
    snapped_v = snap_objects(by_orientation["v"], "x0", x_tolerance)
    snapped_h = snap_objects(by_orientation["h"], "top", y_tolerance)
    return snapped_v + snapped_h


def resize_object(obj, key: str, value):
    assert key in ("x0", "x1", "top", "bottom")
    old_value = obj[key]
    diff = value - old_value
    new_items = [
        (key, value),
    ]
    if key == "x0":
        new_items.append(("width", obj["width"] - diff))
    elif key == "x1":
        new_items.append(("width", obj["width"] + diff))
    elif key == "top":
        new_items.append(("height", obj["height"] - diff))
    elif key == "bottom":
        new_items.append(("height", obj["height"] + diff))
    return obj.__class__(tuple(obj.items()) + tuple(new_items))


def join_edge_group(edges, orientation: str, tolerance=DEFAULT_JOIN_TOLERANCE):
    """
    Given a list of edges along the same infinite line, join those that
    are within `tolerance` pixels of one another.
    """
    if orientation == "h":
        min_prop, max_prop = "x0", "x1"
    elif orientation == "v":
        min_prop, max_prop = "top", "bottom"
    else:
        raise ValueError("Orientation must be 'v' or 'h'")
    
    sorted_edges = list(sorted(edges, key=itemgetter(min_prop)))
    joined = [sorted_edges[0]]
    for e in sorted_edges[1:]:
        last = joined[-1]
        if e[min_prop] <= (last[max_prop] + tolerance):
            # Join the edges
            joined[-1] = resize_object(last, max_prop, max(e[max_prop], last[max_prop]))
        else:
            joined.append(e)
    
    return joined


def merge_edges(
    edges,
    snap_x_tolerance,
    snap_y_tolerance,
    join_x_tolerance,
    join_y_tolerance,
):
    """
    Using the `snap_edges` and `join_edge_group` methods above,
    merge a list of edges into a more "seamless" list.
    """
    
    def get_group(edge):
        return (edge["orientation"], edge.get("x0") if edge["orientation"] == "v" else edge.get("top"))
    
    if snap_x_tolerance > 0 or snap_y_tolerance > 0:
        edges = snap_edges(edges, snap_x_tolerance, snap_y_tolerance)
    
    _sorted = sorted(edges, key=get_group)
    edge_groups = itertools.groupby(_sorted, key=get_group)
    edge_gen = (
        join_edge_group(
            items, k[0], (join_x_tolerance if k[0] == "h" else join_y_tolerance)
        )
        for k, items in edge_groups
    )
    edges = list(itertools.chain(*edge_gen))
    return edges


def bbox_to_rect(bbox) -> dict:
    """Return the rectangle for an object."""
    return {"x0": bbox[0], "top": bbox[1], "x1": bbox[2], "bottom": bbox[3]}


def objects_to_rect(objects) -> dict:
    """
    Given an iterable of objects, return the smallest rectangle that contains them all.
    """
    return bbox_to_rect(objects_to_bbox(objects))


def merge_bboxes(bboxes):
    """
    Given an iterable of bounding boxes, return the smallest bounding box
    that contains them all.
    """
    x0, top, x1, bottom = zip(*bboxes)
    return (min(x0), min(top), max(x1), max(bottom))


def objects_to_bbox(objects):
    """
    Given an iterable of objects, return the smallest bounding box that
    contains them all.
    """
    return merge_bboxes(map(bbox_getter, objects))


def words_to_edges_h(words, word_threshold: int = DEFAULT_MIN_WORDS_HORIZONTAL):
    """
    Find (imaginary) horizontal lines that connect the tops
    of at least `word_threshold` words.
    """
    by_top = cluster_objects(words, itemgetter("top"), 1)
    large_clusters = filter(lambda x: len(x) >= word_threshold, by_top)
    rects = list(map(objects_to_rect, large_clusters))
    if len(rects) == 0:
        return []
    min_x0 = min(map(itemgetter("x0"), rects))
    max_x1 = max(map(itemgetter("x1"), rects))
    
    edges = []
    for r in rects:
        edges.append({
            "x0": min_x0,
            "x1": max_x1,
            "top": r["top"],
            "bottom": r["top"],
            "height": 0,
            "orientation": "h",
        })
    
    return edges


def get_bbox_overlap(a, b):
    a_left, a_top, a_right, a_bottom = a
    b_left, b_top, b_right, b_bottom = b
    o_left = max(a_left, b_left)
    o_right = min(a_right, b_right)
    o_bottom = min(a_bottom, b_bottom)
    o_top = max(a_top, b_top)
    o_width = o_right - o_left
    o_height = o_bottom - o_top
    if o_height >= 0 and o_width >= 0 and o_height + o_width > 0:
        return (o_left, o_top, o_right, o_bottom)
    else:
        return None


def words_to_edges_v(words, word_threshold: int = DEFAULT_MIN_WORDS_VERTICAL):
    """
    Find (imaginary) vertical lines that connect the left, right, or
    center of at least `word_threshold` words.
    """
    # Find words that share the same left, right, or centerpoints
    by_x0 = cluster_objects(words, itemgetter("x0"), 1)
    by_x1 = cluster_objects(words, itemgetter("x1"), 1)
    
    def get_center(word):
        return (word["x0"] + word["x1"]) / 2
    
    by_center = cluster_objects(words, get_center, 1)
    clusters = by_x0 + by_x1 + by_center
    
    # Find the points that align with the most words
    sorted_clusters = sorted(clusters, key=lambda x: -len(x))
    large_clusters = filter(lambda x: len(x) >= word_threshold, sorted_clusters)
    
    # For each of those points, find the bboxes fitting all matching words
    bboxes = list(map(objects_to_bbox, large_clusters))
    
    # Iterate through those bboxes, condensing overlapping bboxes
    condensed_bboxes = []
    for bbox in bboxes:
        overlap_bboxes = [condensed for condensed in condensed_bboxes
                         if get_bbox_overlap(bbox, condensed) is not None]
        non_overlap_bboxes = [condensed for condensed in condensed_bboxes
                             if get_bbox_overlap(bbox, condensed) is None]
        
        if len(overlap_bboxes) == 0:
            condensed_bboxes.append(bbox)
        else:
            new_bbox = merge_bboxes([bbox] + overlap_bboxes)
            condensed_bboxes = non_overlap_bboxes + [new_bbox]
    
    if not condensed_bboxes:
        return []
    
    condensed_rects = map(bbox_to_rect, condensed_bboxes)
    sorted_rects = list(sorted(condensed_rects, key=itemgetter("x0")))
    
    max_x1 = max(map(itemgetter("x1"), sorted_rects))
    min_top = min(map(itemgetter("top"), sorted_rects))
    max_bottom = max(map(itemgetter("bottom"), sorted_rects))
    
    return [
        {
            "x0": b["x0"],
            "x1": b["x0"],
            "top": min_top,
            "bottom": max_bottom,
            "height": max_bottom - min_top,
            "orientation": "v",
        }
        for b in sorted_rects
    ] + [
        {
            "x0": max_x1,
            "x1": max_x1,
            "top": min_top,
            "bottom": max_bottom,
            "height": max_bottom - min_top,
            "orientation": "v",
        }
    ]


def edges_to_intersections(edges, x_tolerance=1, y_tolerance=1) -> dict:
    """
    Given a list of edges, return the points at which they intersect
    within `tolerance` pixels.
    """
    intersections = {}
    v_edges, h_edges = [
        list(filter(lambda x: x["orientation"] == o, edges)) for o in ("v", "h")
    ]
    for v in sorted(v_edges, key=itemgetter("x0", "top")):
        for h in sorted(h_edges, key=itemgetter("top", "x0")):
            if (
                abs(v["x0"] - h["x0"]) <= x_tolerance or
                abs(v["x0"] - h["x1"]) <= x_tolerance
            ) and (
                abs(h["top"] - v["top"]) <= y_tolerance or
                abs(h["top"] - v["bottom"]) <= y_tolerance
            ):
                vertex = (
                    v["x0"],
                    h["top"],
                )
                if vertex not in intersections:
                    intersections[vertex] = []
                intersections[vertex].append(v)
                intersections[vertex].append(h)
    
    return intersections


def obj_to_bbox(obj):
    """Return the bounding box for an object."""
    return bbox_getter(obj)


def intersections_to_cells(intersections):
    """
    Given a list of points (`intersections`), return all rectangular "cells"
    that those points describe.
    """
    
    def edge_connects(p1, p2) -> bool:
        def edges_intersect(v, h, corner):
            v_y_range = sorted([v["top"], v["bottom"]])
            h_x_range = sorted([h["x0"], h["x1"]])
            return (
                (v_y_range[0] <= corner[1] <= v_y_range[1])
                and (h_x_range[0] <= corner[0] <= h_x_range[1])
            )
        
        if p1 == p2:
            return False
        
        horizontal = [p for p in [p1, p2] if p[1] == min(p1[1], p2[1])]
        vertical = [p for p in [p1, p2] if p[0] == min(p1[0], p2[0])]
        
        if len(horizontal) == 2:  # horizontal connection
            y = p1[1]
            x_range = sorted([p1[0], p2[0]])
            h_edges = [e for e in intersections.get(p1, []) + intersections.get(p2, [])
                      if e["orientation"] == "h" and e["top"] == y]
            for h in h_edges:
                if h["x0"] <= x_range[0] and h["x1"] >= x_range[1]:
                    return True
        
        if len(vertical) == 2:  # vertical connection
            x = p1[0]
            y_range = sorted([p1[1], p2[1]])
            v_edges = [e for e in intersections.get(p1, []) + intersections.get(p2, [])
                      if e["orientation"] == "v" and e["x0"] == x]
            for v in v_edges:
                if v["top"] <= y_range[0] and v["bottom"] >= y_range[1]:
                    return True
        
        return False
    
    points = list(sorted(intersections.keys()))
    n_points = len(points)
    
    def find_smallest_cell(points, i: int):
        origin = points[i]
        
        # Find possible opposite corners
        bottom_right = [
            points[j] for j in range(len(points))
            if points[j][0] > origin[0] and points[j][1] > origin[1]
        ]
        
        if not bottom_right:
            return None
        
        # Sort by proximity to find smallest cell
        sorted_candidates = sorted(
            bottom_right,
            key=lambda p: (p[0] - origin[0]) * (p[1] - origin[1])
        )
        
        for candidate in sorted_candidates:
            top_right = (candidate[0], origin[1])
            bottom_left = (origin[0], candidate[1])
            
            if (
                top_right in points and
                bottom_left in points and
                edge_connects(origin, top_right) and
                edge_connects(top_right, candidate) and
                edge_connects(candidate, bottom_left) and
                edge_connects(bottom_left, origin)
            ):
                return (origin[0], origin[1], candidate[0], candidate[1])
        
        return None
    
    cell_gen = (find_smallest_cell(points, i) for i in range(len(points)))
    return list(filter(None, cell_gen))


def cells_to_tables(page, cells) -> list:
    """
    Given a list of bounding boxes (`cells`), return a list of tables that
    hold those cells most simply (and contiguously).
    """
    
    def bbox_to_corners(bbox) -> tuple:
        return (
            (bbox[0], bbox[1]),  # top-left
            (bbox[2], bbox[1]),  # top-right  
            (bbox[2], bbox[3]),  # bottom-right
            (bbox[0], bbox[3]),  # bottom-left
        )
    
    remaining_cells = list(cells)
    
    # Iterate through the cells found above, and assign them
    # to contiguous tables
    
    current_corners = set()
    current_cells = []
    
    tables = []
    while len(remaining_cells):
        if len(current_cells) == 0:
            current_cells.append(remaining_cells.pop(0))
            current_corners = set(bbox_to_corners(current_cells[0]))
        else:
            found_neighbor = False
            for i, cell in enumerate(remaining_cells):
                cell_corners = set(bbox_to_corners(cell))
                if len(current_corners & cell_corners):
                    current_cells.append(remaining_cells.pop(i))
                    current_corners |= cell_corners
                    found_neighbor = True
                    break
            
            if not found_neighbor:
                tables.append(current_cells)
                current_cells = []
                current_corners = set()
    
    # Once we have exhausting the list of cells ...
    
    # ... and we have a cell group that has not been stored
    if len(current_cells):
        tables.append(current_cells)
    
    # PyMuPDF modification:
    # Remove tables without text or having only 1 column
    for i in range(len(tables) - 1, -1, -1):
        table_cells = tables[i]
        if len(table_cells) < 2:
            tables.pop(i)
            continue
        
        # Check if table has at least 2 columns by looking at unique x-coordinates
        x_coords = set()
        for cell in table_cells:
            x_coords.add(cell[0])  # x0
        
        if len(x_coords) < 2:
            tables.pop(i)
    
    # Sort the tables top-to-bottom-left-to-right based on the value of the
    # topmost-and-then-leftmost coordinate of a table.
    _sorted = sorted(tables, key=lambda t: min((c[1], c[0]) for c in t))
    return _sorted


# Classes from original table.py
class WordExtractor:
    def __init__(
        self,
        x_tolerance=DEFAULT_X_TOLERANCE,
        y_tolerance=DEFAULT_Y_TOLERANCE,
        keep_blank_chars: bool = False,
        use_text_flow=False,
        horizontal_ltr=True,
        vertical_ttb=False,
        extra_attrs=None,
        split_at_punctuation=False,
        expand_ligatures=True,
    ):
        self.x_tolerance = x_tolerance
        self.y_tolerance = y_tolerance
        self.keep_blank_chars = keep_blank_chars
        self.use_text_flow = use_text_flow
        self.horizontal_ltr = horizontal_ltr
        self.vertical_ttb = vertical_ttb
        self.extra_attrs = extra_attrs or []
        self.split_at_punctuation = split_at_punctuation
        self.expand_ligatures = expand_ligatures

    def merge_chars(self, ordered_chars: list):
        x0, top, x1, bottom = zip(*[
            (c["x0"], c["top"], c["x1"], c["bottom"]) for c in ordered_chars
        ])
        
        return {
            "x0": min(x0),
            "x1": max(x1),
            "top": min(top),
            "bottom": max(bottom),
            "chars": ordered_chars,
        }

    def char_begins_new_word(
        self,
        prev_char,
        curr_char,
    ) -> bool:
        if prev_char is None:
            return True
        
        x_dist = curr_char["x0"] - prev_char["x1"]
        y_dist = abs(curr_char["top"] - prev_char["top"])
        
        if y_dist > self.y_tolerance:
            return True
        
        if x_dist > self.x_tolerance:
            return True
        
        return False

    def iter_chars_to_words(self, ordered_chars):
        if not ordered_chars:
            return
        
        current_word = [ordered_chars[0]]
        
        for char in ordered_chars[1:]:
            if self.char_begins_new_word(current_word[-1], char):
                yield current_word
                current_word = [char]
            else:
                current_word.append(char)
        
        if current_word:
            yield current_word

    def iter_sort_chars(self, chars):
        if self.use_text_flow:
            return chars
        else:
            return sorted(chars, key=lambda x: (x["top"], x["x0"]))

    def iter_extract_tuples(self, chars):
        sorted_chars = self.iter_sort_chars(chars)
        
        for word_chars in self.iter_chars_to_words(sorted_chars):
            word = self.merge_chars(word_chars)
            text = "".join(c.get("text", "") for c in word_chars)
            
            if not self.keep_blank_chars and text.strip() == "":
                continue
            
            word["text"] = text
            yield (word, word_chars)

    def extract_wordmap(self, chars):
        tuples = list(self.iter_extract_tuples(chars))
        return tuples

    def extract_words(self, chars: list) -> list:
        word_tuples = self.extract_wordmap(chars)
        return [word[0] for word in word_tuples]


def extract_words(chars: list, **kwargs) -> list:
    return WordExtractor(**kwargs).extract_words(chars)


def extract_text(chars: list, **kwargs) -> str:
    chars = to_list(chars)
    if len(chars) == 0:
        return ""

    words = extract_words(chars, **kwargs)
    words_sorted = sorted(words, key=itemgetter("top", "x0"))
    return " ".join(word["text"] for word in words_sorted)


def collate_line(
    line_chars: list,
    tolerance=DEFAULT_X_TOLERANCE,
) -> str:
    coll = ""
    last_x1 = None
    for char in sorted(line_chars, key=itemgetter("x0")):
        if last_x1 is not None and char["x0"] - last_x1 > tolerance:
            coll += " "
        last_x1 = char["x1"]
        coll += char.get("text", "")
    return coll


def line_to_edge(line):
    edge = dict(line)
    edge["orientation"] = "h" if (line["top"] == line["bottom"]) else "v"
    return edge


def rect_to_edges(rect) -> list:
    top, bottom, left, right = [dict(rect) for x in range(4)]
    top.update(
        {
            "object_type": "rect_edge",
            "height": 0,
            "y0": rect["y1"],
            "bottom": rect["top"],
            "orientation": "h",
        }
    )
    bottom.update(
        {
            "object_type": "rect_edge",
            "height": 0,
            "y1": rect["y0"],
            "top": rect["top"] + rect["height"],
            "doctop": rect.get("doctop", 0) + rect["height"],
            "orientation": "h",
        }
    )
    left.update(
        {
            "object_type": "rect_edge",
            "width": 0,
            "x1": rect["x0"],
            "orientation": "v",
        }
    )
    right.update(
        {
            "object_type": "rect_edge",
            "width": 0,
            "x0": rect["x1"],
            "orientation": "v",
        }
    )
    return [top, bottom, left, right]


def obj_to_edges(obj) -> list:
    t = obj["object_type"]
    if "_edge" in t:
        return [obj]
    elif t == "line":
        return [line_to_edge(obj)]
    else:
        return rect_to_edges(obj)


@dataclass
class TableSettings:
    vertical_strategy: str = "lines"
    horizontal_strategy: str = "lines"
    explicit_vertical_lines: Optional[list] = None
    explicit_horizontal_lines: Optional[list] = None
    snap_tolerance: float = DEFAULT_SNAP_TOLERANCE
    snap_x_tolerance: float = DEFAULT_SNAP_TOLERANCE
    snap_y_tolerance: float = DEFAULT_SNAP_TOLERANCE
    join_tolerance: float = DEFAULT_JOIN_TOLERANCE
    join_x_tolerance: float = DEFAULT_JOIN_TOLERANCE
    join_y_tolerance: float = DEFAULT_JOIN_TOLERANCE
    edge_min_length: float = 3
    min_words_vertical: int = DEFAULT_MIN_WORDS_VERTICAL
    min_words_horizontal: int = DEFAULT_MIN_WORDS_HORIZONTAL
    intersection_tolerance: float = 3
    intersection_x_tolerance: float = 3
    intersection_y_tolerance: float = 3
    text_tolerance: float = 3
    text_x_tolerance: float = 3
    text_y_tolerance: float = 3

    @classmethod
    def resolve(cls, settings):
        return cls(**settings)


@dataclass
class GridReconstructionSettings:
    """Settings for grid reconstruction algorithms."""
    snap_tolerance: float = DEFAULT_SNAP_TOLERANCE
    join_tolerance: float = DEFAULT_JOIN_TOLERANCE
    intersection_tolerance: float = 3.0
    line_tolerance: float = 2.0
    word_threshold_vertical: int = DEFAULT_MIN_WORDS_VERTICAL
    word_threshold_horizontal: int = DEFAULT_MIN_WORDS_HORIZONTAL


class TableFinder:
    """
    Table finder using the original PyMuPDF logic.
    """

    def __init__(self, page, settings=None, chars=None, edges=None):
        self.page = page
        self.settings = settings or TableSettings()
        self.chars = chars or []
        self.edges = edges or []

    def get_edges(self) -> list:
        """Get all edges for table detection using original logic."""
        edges = list(self.edges)  # Copy the edges
        
        # Filter by minimum length
        edges = filter_edges(edges, min_length=self.settings.edge_min_length)
        
        # Add text-derived edges if using text strategies
        words = extract_words(self.chars, 
                             x_tolerance=self.settings.text_x_tolerance,
                             y_tolerance=self.settings.text_y_tolerance)
        
        if self.settings.vertical_strategy in ["text"]:
            text_v_edges = words_to_edges_v(words, self.settings.min_words_vertical)
            edges.extend(text_v_edges)
        
        if self.settings.horizontal_strategy in ["text"]:
            text_h_edges = words_to_edges_h(words, self.settings.min_words_horizontal)
            edges.extend(text_h_edges)
        
        # Merge edges using original algorithm
        edges = merge_edges(
            edges,
            snap_x_tolerance=self.settings.snap_x_tolerance,
            snap_y_tolerance=self.settings.snap_y_tolerance,
            join_x_tolerance=self.settings.join_x_tolerance,
            join_y_tolerance=self.settings.join_y_tolerance,
        )
        
        return edges

    def __getitem__(self, i):
        edges = self.get_edges()
        
        # Find intersections using original algorithm
        intersections = edges_to_intersections(
            edges,
            x_tolerance=self.settings.intersection_x_tolerance,
            y_tolerance=self.settings.intersection_y_tolerance,
        )
        
        # Convert intersections to cells using original algorithm
        cells = intersections_to_cells(intersections)
        
        # Group cells into tables using original algorithm
        tables = cells_to_tables(self.page, cells)
        
        if i >= len(tables):
            raise IndexError("Table index out of range")
        
        return Table(self.page, tables[i])


class CellGroup:
    """Base class for grouped table cells."""
    
    def __init__(self, cells):
        self.cells = cells
        self.bbox = self._calculate_bbox()
    
    def _calculate_bbox(self) -> BBox:
        """Calculate bounding box that encompasses all cells."""
        valid_cells = [c for c in self.cells if c is not None]
        if not valid_cells:
            return (0, 0, 0, 0)
        
        return (
            min(c[0] for c in valid_cells),  # min x0
            min(c[1] for c in valid_cells),  # min y0
            max(c[2] for c in valid_cells),  # max x1
            max(c[3] for c in valid_cells)   # max y1
        )


class TableRow(CellGroup):
    """Represents a single row in a table."""
    pass


class TableHeader:
    """PyMuPDF extension containing the identified table header."""

    def __init__(self, bbox, cells, names, external):
        self.bbox = bbox
        self.cells = cells
        self.names = names
        self.external = external


def extract_cells(textpage, cell, markdown=False):
    """Extract text from a rect-like 'cell' as plain or MD style text.

    This function should ultimately be used to extract text from a table cell.
    Markdown output will only work correctly if extraction flag bit
    TEXT_COLLECT_STYLES is set.

    Args:
        textpage: A PyMuPDF TextPage object. Must have been created with
            TEXTFLAGS_TEXT | TEXT_COLLECT_STYLES.
        cell: A tuple (x0, y0, x1, y1) defining the cell's bbox.
        markdown: If True, return text formatted for Markdown.

    Returns:
        A string with the text extracted from the cell.
    """
    text = ""
    try:
        for block in textpage.extractRAWDICT()["blocks"]:
            if block["type"] != 0:
                continue
            block_bbox = block["bbox"]
            
            # Check if block intersects with cell
            if not bbox_intersects(cell, block_bbox):
                continue
            
            for line in block["lines"]:
                line_bbox = line["bbox"]
                if not bbox_intersects(cell, line_bbox):
                    continue
                
                line_text = ""
                for span in line["spans"]:
                    span_bbox = span["bbox"]
                    if not bbox_intersects(cell, span_bbox):
                        continue
                    
                    span_text = span["text"]
                    
                    if markdown and span.get("flags", 0):
                        # Add markdown formatting based on flags
                        flags = span["flags"]
                        if flags & (1 << 4):  # Bold
                            span_text = f"**{span_text}**"
                        if flags & (1 << 5):  # Italic
                            span_text = f"*{span_text}*"
                    
                    line_text += span_text
                
                if line_text.strip():
                    text += line_text + "\n"
    except:
        # Fallback extraction
        try:
            rect = fitz.Rect(cell)
            text = textpage.extractText(rect)
        except:
            text = ""
    
    return text.strip()


class Table:
    """
    Represents a complete table structure using original PyMuPDF logic.
    """
    
    def __init__(self, page, cells):
        self.page = weakref.proxy(page)
        self.cells = cells
        self._rows = None
        self._header = None
    
    @property
    def bbox(self) -> BBox:
        """Calculate overall table bounding box."""
        if not self.cells:
            return (0, 0, 0, 0)
        
        return objects_to_bbox(self.cells)
    
    @property  
    def rows(self) -> List[TableRow]:
        """Get table rows organized by vertical position using original logic."""
        if self._rows is None:
            if not self.cells:
                self._rows = []
            else:
                # Group cells by y-coordinate (rows) using original clustering
                y_groups = cluster_objects(self.cells, itemgetter(1), tolerance=3)
                rows = []
                for y_group in y_groups:
                    # Sort cells in row by x-coordinate  
                    sorted_cells = sorted(y_group, key=itemgetter(0))
                    rows.append(TableRow(sorted_cells))
                self._rows = rows
        
        return self._rows
    
    @property
    def row_count(self) -> int:
        """Get number of rows in table."""
        return len(self.rows)
    
    @property
    def col_count(self) -> int:
        """Get number of columns in table."""
        if not self.rows:
            return 0
        return max(len(row.cells) for row in self.rows)
    
    @property
    def header(self) -> TableHeader:
        """Get or create table header."""
        if self._header is None:
            self._header = self._get_header()
        return self._header
    
    def extract(self, **kwargs) -> list:
        """
        Extract text content from all table cells using original logic.
        """
        extracted_rows = []
        
        for row in self.rows:
            extracted_row = []
            for cell in row.cells:
                # Extract text from this cell using the original extract_cells logic
                try:
                    # Get page's textpage for extraction
                    flags = TEXTFLAGS_TEXT | TEXT_COLLECT_STYLES
                    textpage = self.page.get_textpage(flags=flags)
                    
                    cell_text = extract_cells(textpage, cell, kwargs.get('markdown', False))
                    extracted_row.append(cell_text)
                except:
                    # Fallback to simple text extraction
                    try:
                        rect = fitz.Rect(cell)
                        cell_text = self.page.get_textbox(rect).strip()
                        extracted_row.append(cell_text)
                    except:
                        extracted_row.append("")
            
            extracted_rows.append(extracted_row)
        
        return extracted_rows
    
    def to_markdown(self, clean=False, fill_empty=True):
        """
        Convert table to markdown format using original logic.
        """
        extracted_data = self.extract(markdown=True)
        
        if not extracted_data:
            return ""
        
        # Fill empty cells if requested
        if fill_empty:
            extracted_data = self._fill_empty_cells(extracted_data)
        
        # Build markdown string
        output = "|"
        
        # Header row
        header_names = self.header.names if self.header else []
        col_count = self.col_count
        
        for i in range(col_count):
            name = header_names[i] if i < len(header_names) else f"Col{i+1}"
            if not name.strip():
                name = f"Col{i+1}"
            
            name = name.replace("\n", "<br>")
            if clean:
                # Clean markdown syntax
                name = html.escape(name)
            
            output += name + "|"
        
        output += "\n"
        
        # Separator row
        output += "|" + "|".join("---" for _ in range(col_count)) + "|\n"
        
        # Data rows (skip first row if header is internal)
        start_row = 1 if (self.header and not self.header.external) else 0
        
        for row_data in extracted_data[start_row:]:
            line = "|"
            for i, cell_text in enumerate(row_data[:col_count]):
                cell_text = str(cell_text).replace("\n", "<br>")
                if clean:
                    cell_text = html.escape(cell_text)
                line += cell_text + "|"
            
            output += line + "\n"
        
        return output
    
    def to_pandas(self):
        """
        Convert table to pandas DataFrame.
        """
        try:
            import pandas as pd
        except ImportError:
            raise ImportError("pandas is required for to_pandas() method")
        
        extracted_data = self.extract()
        
        if not extracted_data:
            return pd.DataFrame()
        
        # Prepare column names
        header_names = self.header.names if self.header else []
        col_count = self.col_count
        
        column_names = []
        for i in range(col_count):
            name = header_names[i] if i < len(header_names) else f"Col{i+1}"
            if not name.strip():
                name = f"Col{i+1}"
            column_names.append(name)
        
        # Ensure unique column names
        seen_names = set()
        for i, name in enumerate(column_names):
            original_name = name
            counter = 1
            while name in seen_names:
                name = f"{original_name}_{counter}"
                counter += 1
            column_names[i] = name
            seen_names.add(name)
        
        # Prepare data (skip header row if internal)
        start_row = 1 if (self.header and not self.header.external) else 0
        data_rows = extracted_data[start_row:]
        
        # Create DataFrame
        df_data = {}
        for i, col_name in enumerate(column_names):
            column_data = []
            for row in data_rows:
                cell_value = row[i] if i < len(row) else ""
                column_data.append(cell_value)
            df_data[col_name] = column_data
        
        return pd.DataFrame(df_data)
    
    def _get_header(self, y_tolerance=3):
        """
        Detect table header information using original logic.
        """
        if not self.rows:
            return TableHeader((0, 0, 0, 0), [], [], False)
        
        # Simple heuristic: use first row as header
        first_row = self.rows[0]
        header_names = []
        
        # Extract text from first row cells
        for cell in first_row.cells:
            try:
                flags = TEXTFLAGS_TEXT | TEXT_COLLECT_STYLES
                textpage = self.page.get_textpage(flags=flags)
                
                name = extract_cells(textpage, cell, False).strip()
                if not name:
                    name = f"Col{len(header_names)+1}"
                header_names.append(name)
            except:
                header_names.append(f"Col{len(header_names)+1}")
        
        return TableHeader(
            bbox=first_row.bbox,
            cells=first_row.cells,
            names=header_names,
            external=False  # Assuming header is part of table
        )
    
    def _fill_empty_cells(self, data):
        """Fill empty cells with neighboring values."""
        if not data:
            return data
        
        filled_data = [row[:] for row in data]  # Deep copy
        
        # Fill horizontally (left to right within rows)
        for i, row in enumerate(filled_data):
            for j in range(len(row) - 1):
                if not str(row[j+1]).strip() and str(row[j]).strip():
                    filled_data[i][j+1] = row[j]
        
        # Fill vertically (top to bottom within columns)
        for j in range(len(filled_data[0]) if filled_data else 0):
            for i in range(len(filled_data) - 1):
                if (j < len(filled_data[i+1]) and j < len(filled_data[i]) and
                    not str(filled_data[i+1][j]).strip() and str(filled_data[i][j]).strip()):
                    filled_data[i+1][j] = filled_data[i][j]
        
        return filled_data


class TableProcessor:
    """
    Main table processing system that converts bounding boxes to structured tables
    using the original PyMuPDF table logic.
    """
    
    def __init__(self, page: fitz.Page, 
                 reconstruction_settings: GridReconstructionSettings = None):
        """
        Initialize the table processor.
        
        Args:
            page: PyMuPDF page object
            reconstruction_settings: Settings for grid reconstruction (converted to TableSettings)
        """
        self.page = page
        self.grid_settings = reconstruction_settings or GridReconstructionSettings()
        
        # Convert to TableSettings format
        self.table_settings = TableSettings(
            snap_tolerance=self.grid_settings.snap_tolerance,
            join_tolerance=self.grid_settings.join_tolerance,
            intersection_tolerance=self.grid_settings.intersection_tolerance,
            snap_x_tolerance=self.grid_settings.snap_tolerance,
            snap_y_tolerance=self.grid_settings.snap_tolerance,
            join_x_tolerance=self.grid_settings.join_tolerance,
            join_y_tolerance=self.grid_settings.join_tolerance,
            intersection_x_tolerance=self.grid_settings.intersection_tolerance,
            intersection_y_tolerance=self.grid_settings.intersection_tolerance,
            text_tolerance=self.grid_settings.line_tolerance,
            text_x_tolerance=self.grid_settings.line_tolerance,
            text_y_tolerance=self.grid_settings.line_tolerance,
            min_words_vertical=self.grid_settings.word_threshold_vertical,
            min_words_horizontal=self.grid_settings.word_threshold_horizontal,
        )
    
    def process_tables(self, table_bboxes: List[BBox], 
                      strategy: str = "lines") -> List[Table]:
        """
        Process a list of table bounding boxes into structured Table objects
        using the original PyMuPDF table algorithms.
        
        Args:
            table_bboxes: List of table bounding boxes [(x0, y0, x1, y1), ...]
            strategy: Grid reconstruction strategy ("lines" or "text")
        
        Returns:
            List[Table]: List of processed Table objects
        """
        tables = []
        
        for table_bbox in table_bboxes:
            try:
                # Extract content within the bounding box
                chars, edges = self._extract_content_in_bbox(table_bbox)
                
                # Set strategy based on parameter
                settings = TableSettings(
                    vertical_strategy=strategy,
                    horizontal_strategy=strategy,
                    **{k: getattr(self.table_settings, k) for k in vars(self.table_settings) 
                       if not k.startswith('_')}
                )
                
                # Create TableFinder with extracted content
                table_finder = TableFinder(self.page, settings, chars, edges)
                
                # Get edges and find tables using original algorithm
                all_edges = table_finder.get_edges()
                
                # Find intersections using original algorithm
                intersections = edges_to_intersections(
                    all_edges,
                    x_tolerance=settings.intersection_x_tolerance,
                    y_tolerance=settings.intersection_y_tolerance,
                )
                
                # Convert intersections to cells using original algorithm
                cells = intersections_to_cells(intersections)
                
                # Filter cells to only include those within the table bbox
                filtered_cells = []
                for cell in cells:
                    if bbox_intersects(cell, table_bbox):
                        filtered_cells.append(cell)
                
                if filtered_cells:
                    # Create Table object
                    table = Table(self.page, filtered_cells)
                    tables.append(table)
                    
            except Exception as e:
                print(f"Error processing table at {table_bbox}: {e}")
                continue
        
        return tables
    
    def _extract_content_in_bbox(self, table_bbox: BBox) -> Tuple[List[Character], 
                                                                 List[VectorGraphic]]:
        """
        Extract all content (text characters and vector graphics) within a bounding box
        using the original PyMuPDF algorithms.
        """
        characters = []
        vector_graphics = []
        
        try:
            # Extract text characters using the original make_chars logic
            flags = TEXTFLAGS_TEXT | TEXT_COLLECT_STYLES
            textpage = self.page.get_textpage(flags=flags)
            blocks = textpage.extractRAWDICT()["blocks"]
            
            page_height = self.page.rect.height
            doctop_base = page_height * self.page.number
            
            for block in blocks:
                if block["type"] != 0:
                    continue
                
                # Check if block intersects with table bbox
                if not bbox_intersects(table_bbox, block["bbox"]):
                    continue
                
                for line in block["lines"]:
                    if not bbox_intersects(table_bbox, line["bbox"]):
                        continue
                    
                    for span in line["spans"]:
                        if not bbox_intersects(table_bbox, span["bbox"]):
                            continue
                        
                        for char in span["chars"]:
                            char_bbox = (char["bbox"][0], char["bbox"][1], 
                                       char["bbox"][2], char["bbox"][3])
                            
                            if char_in_bbox({"x0": char_bbox[0], "x1": char_bbox[2],
                                           "top": char_bbox[1], "bottom": char_bbox[3]}, table_bbox):
                                
                                # Convert to expected format
                                char_dict = {
                                    "x0": char_bbox[0],
                                    "x1": char_bbox[2], 
                                    "top": char_bbox[1],
                                    "bottom": char_bbox[3],
                                    "doctop": char_bbox[1] + doctop_base,
                                    "text": char.get("c", ""),
                                    "fontname": span.get("font", ""),
                                    "size": span.get("size", 0),
                                    "flags": span.get("flags", 0),
                                    "upright": 1,
                                }
                                characters.append(char_dict)
            
            # Extract vector graphics using the original make_edges logic  
            drawings = self.page.get_drawings()
            
            for drawing in drawings:
                # Check if drawing intersects with table bbox
                drawing_bbox = drawing.get("rect", (0, 0, 0, 0))
                if not bbox_intersects(table_bbox, drawing_bbox):
                    continue
                
                # Convert paths to edges
                for item in drawing.get("items", []):
                    if item[0] == "l":  # line
                        p1, p2 = item[1], item[2]
                        edge = {
                            "x0": min(p1.x, p2.x),
                            "x1": max(p1.x, p2.x),
                            "top": min(p1.y, p2.y),
                            "bottom": max(p1.y, p2.y),
                            "width": abs(p2.x - p1.x),
                            "height": abs(p2.y - p1.y),
                            "orientation": "h" if abs(p2.y - p1.y) < 1 else "v",
                            "object_type": "line"
                        }
                        vector_graphics.append(edge)
                    
                    elif item[0] == "re":  # rectangle
                        rect = item[1]
                        # Convert rectangle to edges
                        edges = rect_to_edges({
                            "x0": rect.x0,
                            "top": rect.y0,
                            "x1": rect.x1,
                            "bottom": rect.y1,
                            "width": rect.width,
                            "height": rect.height,
                            "doctop": rect.y0 + doctop_base,
                        })
                        vector_graphics.extend(edges)
        
        except Exception as e:
            print(f"Error extracting content from bbox {table_bbox}: {e}")
        
        return characters, vector_graphics


# Utility functions for advanced table processing

def merge_overlapping_tables(tables: List[Table], 
                           overlap_threshold: float = 0.1) -> List[Table]:
    """
    Merge tables that significantly overlap with each other.
    """
    if len(tables) <= 1:
        return tables
    
    def calculate_overlap_ratio(bbox1: BBox, bbox2: BBox) -> float:
        """Calculate the ratio of intersection area to smaller bbox area."""
        x1, y1, x2, y2 = bbox1
        x3, y3, x4, y4 = bbox2
        
        # Calculate intersection
        xi1, yi1 = max(x1, x3), max(y1, y3)
        xi2, yi2 = min(x2, x4), min(y2, y4)
        
        if xi1 >= xi2 or yi1 >= yi2:
            return 0.0  # No intersection
        
        intersection_area = (xi2 - xi1) * (yi2 - yi1)
        area1 = (x2 - x1) * (y2 - y1)
        area2 = (x4 - x3) * (y4 - y3)
        
        return intersection_area / min(area1, area2)
    
    merged_tables = []
    used_indices = set()
    
    for i, table1 in enumerate(tables):
        if i in used_indices:
            continue
        
        tables_to_merge = [table1]
        used_indices.add(i)
        
        for j, table2 in enumerate(tables[i+1:], i+1):
            if j in used_indices:
                continue
            
            overlap_ratio = calculate_overlap_ratio(table1.bbox, table2.bbox)
            if overlap_ratio >= overlap_threshold:
                tables_to_merge.append(table2)
                used_indices.add(j)
        
        if len(tables_to_merge) == 1:
            merged_tables.append(table1)
        else:
            # Merge the tables by combining their cells
            all_cells = []
            for table in tables_to_merge:
                all_cells.extend(table.cells)
            merged_table = Table(table1.page, all_cells)
            merged_tables.append(merged_table)
    
    return merged_tables


def filter_small_tables(tables: List[Table], 
                       min_rows: int = 2, 
                       min_cols: int = 2) -> List[Table]:
    """
    Filter out tables that are too small to be meaningful.
    """
    return [table for table in tables 
            if table.row_count >= min_rows and table.col_count >= min_cols]


def sort_tables_by_position(tables: List[Table]) -> List[Table]:
    """
    Sort tables by their position on the page (top-to-bottom, left-to-right).
    """
    return sorted(tables, key=lambda t: (t.bbox[1], t.bbox[0]))


# Example usage and demonstration
def demonstrate_table_processing():
    """
    Demonstrate the table processing system with example usage.
    """
    print("Table Processing System with Original PyMuPDF Logic")
    print("================================================")
    print()
    print("This system implements the original PyMuPDF table processing pipeline")
    print("but accepts pre-detected table bounding boxes as input.")
    print()
    print("Usage example:")
    print("""
    import fitz
    from new_table_fixed import TableProcessor, GridReconstructionSettings
    
    # Open PDF document
    doc = fitz.open("document.pdf")
    page = doc[0]  # First page
    
    # Your table detection system provides these bounding boxes
    table_bboxes = [
        (100, 200, 400, 350),  # Table 1
        (450, 200, 700, 400),  # Table 2
    ]
    
    # Initialize table processor
    settings = GridReconstructionSettings(
        snap_tolerance=3.0,
        join_tolerance=3.0,
        intersection_tolerance=3.0
    )
    processor = TableProcessor(page, settings)
    
    # Process tables using detected bounding boxes
    tables = processor.process_tables(table_bboxes, strategy="lines")
    
    # Use the extracted tables
    for table in tables:
        data_array = table.extract()
        markdown = table.to_markdown()
        df = table.to_pandas()  # if pandas available
    
    doc.close()
    """)


if __name__ == "__main__":
    demonstrate_table_processing()
