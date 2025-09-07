"""
Table Processing System with PyMuPDF

This module implements a comprehensive table processing system that extracts and 
reconstructs table structures from PDF documents using PyMuPDF, starting from 
a list of table bounding boxes.

The implementation follows the mathematical algorithms described for:
1. Content extraction within bounding boxes
2. Grid structure reconstruction (both lines and text strategies)
3. Cell population and content extraction
4. Table structure analysis and output formatting

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


# Constants
DEFAULT_SNAP_TOLERANCE = 3.0
DEFAULT_JOIN_TOLERANCE = 3.0
DEFAULT_MIN_WORDS_VERTICAL = 3
DEFAULT_MIN_WORDS_HORIZONTAL = 1
DEFAULT_X_TOLERANCE = 3
DEFAULT_Y_TOLERANCE = 3
DEFAULT_X_DENSITY = 7.25
DEFAULT_Y_DENSITY = 13
TEXT_FONT_BOLD = 16
TEXT_FONT_SUPERSCRIPT = 1
TEXTFLAGS_TEXT = 4

# Constants from original table.py
UNSET = 0
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
    """
    Check if two bounding boxes intersect.
    
    Args:
        bbox1: First bounding box (x0, y0, x1, y1)
        bbox2: Second bounding box (x0, y0, x1, y1)
    
    Returns:
        bool: True if bounding boxes intersect, False otherwise
    """
    x0_1, y0_1, x1_1, y1_1 = bbox1
    x0_2, y0_2, x1_2, y1_2 = bbox2
    
    return (x0_1 < x1_2) and (x1_1 > x0_2) and (y0_1 < y1_2) and (y1_1 > y0_2)


def char_in_bbox(char: Character, bbox: BBox) -> bool:
    """
    Check if a character's center point falls within a bounding box.
    
    Args:
        char: Character dictionary with 'x0', 'x1', 'top', 'bottom' keys
        bbox: Bounding box (x0, y0, x1, y1)
    
    Returns:
        bool: True if character center is within bbox, False otherwise
    """
    v_mid = (char["top"] + char["bottom"]) / 2
    h_mid = (char["x0"] + char["x1"]) / 2
    x0, y0, x1, y1 = bbox
    
    return (h_mid >= x0) and (h_mid < x1) and (v_mid >= y0) and (v_mid < y1)


def cluster_coordinates(coordinates: List[float], tolerance: float = 3.0) -> List[float]:
    """
    Cluster coordinates that are within tolerance of each other.
    
    Args:
        coordinates: List of coordinate values to cluster
        tolerance: Maximum distance between coordinates in same cluster
    
    Returns:
        List[float]: List of cluster centers (averages)
    """
    if not coordinates:
        return []
    
    sorted_coords = sorted(coordinates)
    clusters = []
    current_cluster = [sorted_coords[0]]
    
    for coord in sorted_coords[1:]:
        if coord - current_cluster[-1] <= tolerance:
            current_cluster.append(coord)
        else:
            clusters.append(sum(current_cluster) / len(current_cluster))
            current_cluster = [coord]
    
    clusters.append(sum(current_cluster) / len(current_cluster))
    return clusters


def extract_text_from_chars(chars: List[Character], **kwargs) -> str:
    """
    Extract and concatenate text from character objects.
    
    Args:
        chars: List of character dictionaries
        **kwargs: Additional formatting options
    
    Returns:
        str: Extracted text string
    """
    if not chars:
        return ""
    
    # Sort characters by position (top-to-bottom, left-to-right)
    sorted_chars = sorted(chars, key=lambda c: (c["top"], c["x0"]))
    
    # Group into lines based on vertical position
    lines = []
    current_line = []
    line_tolerance = kwargs.get("line_tolerance", 2.0)
    
    for char in sorted_chars:
        if not current_line or abs(char["top"] - current_line[-1]["top"]) <= line_tolerance:
            current_line.append(char)
        else:
            lines.append(current_line)
            current_line = [char]
    
    if current_line:
        lines.append(current_line)
    
    # Extract text from each line
    text_lines = []
    for line in lines:
        line_chars = sorted(line, key=lambda c: c["x0"])
        line_text = "".join(c.get("c", "") for c in line_chars)
        text_lines.append(line_text.strip())
    
    return "\n".join(text_lines).strip()


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
    """
    Return the rectangle (i.e a dict with keys "x0", "top", "x1",
    "bottom") for an object.
    """
    return {"x0": bbox[0], "top": bbox[1], "x1": bbox[2], "bottom": bbox[3]}


def objects_to_rect(objects) -> dict:
    """
    Given an iterable of objects, return the smallest rectangle (i.e. a
    dict with "x0", "top", "x1", and "bottom" keys) that contains them
    all.
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
    """
    Return the bounding box for an object.
    """
    return bbox_getter(obj)


def intersections_to_cells(intersections):
    """
    Given a list of points (`intersections`), return all rectangular "cells"
    that those points describe.

    `intersections` should be a dictionary with (x0, top) tuples as keys,
    and a list of edge objects as values. The edge objects should correspond
    to the edges that touch the intersection.
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
class UnsetFloat(float):
    pass


class TextMap:
    """
    A TextMap maps each unicode character in the text to an individual `char`
    object (or, in the case of layout-implied whitespace, `None`).
    """

    def __init__(self, tuples=None) -> None:
        if tuples is None:
            tuples = []
        self.tuples = tuples
        self.as_string = "".join(
            (t[0] if t is not None else "") for t in self.tuples
        )

    def match_to_dict(
        self,
        m,
        main_group: int = 0,
        return_groups: bool = True,
        return_chars: bool = True,
    ) -> dict:
        subset = self.tuples[m.start(main_group) : m.end(main_group)]
        chars = [t[1] for t in subset if t is not None]
        
        groupdict = {"chars": chars} if return_chars else {}
        
        if return_groups:
            groupdict["groups"] = [
                [t[1] for t in self.tuples[m.start(i) : m.end(i)] if t is not None]
                if m.group(i) is not None
                else None
                for i in range(len(m.groups()) + 1)
            ]
        return groupdict


class WordMap:
    """
    A WordMap maps words->chars.
    """

    def __init__(self, tuples) -> None:
        self.tuples = tuples

    def to_textmap(
        self,
        layout: bool = False,
        layout_width=0,
        layout_height=0,
        layout_width_chars: int = 0,
        layout_height_chars: int = 0,
        x_density=DEFAULT_X_DENSITY,
        y_density=DEFAULT_Y_DENSITY,
        x_shift=0,
        y_shift=0,
        y_tolerance=DEFAULT_Y_TOLERANCE,
        use_text_flow: bool = False,
        presorted: bool = False,
        expand_ligatures: bool = True,
    ) -> TextMap:
        if layout:
            raise NotImplementedError("Layout mode not implemented")
        
        if expand_ligatures:
            def expand_ligature(char):
                return LIGATURES.get(char.get("text", ""), char.get("text", ""))
        else:
            def expand_ligature(char):
                return char.get("text", "")
        
        if use_text_flow and presorted:
            sorted_chars = itertools.chain(*[word[1] for word in self.tuples])
        else:
            sorted_chars = sorted(
                itertools.chain(*[word[1] for word in self.tuples]),
                key=lambda x: (x["top"], x["x0"])
            )
        
        text_tuples = []
        for char in sorted_chars:
            char_text = expand_ligature(char)
            for char_char in char_text:
                text_tuples.append((char_char, char))
        
        return TextMap(text_tuples)


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

    def extract_wordmap(self, chars) -> WordMap:
        tuples = list(self.iter_extract_tuples(chars))
        return WordMap(tuples)

    def extract_words(self, chars: list) -> list:
        word_map = self.extract_wordmap(chars)
        return [word[0] for word in word_map.tuples]


def extract_words(chars: list, **kwargs) -> list:
    return WordExtractor(**kwargs).extract_words(chars)


TEXTMAP_KWARGS = ["layout", "layout_width", "layout_height", "layout_width_chars", 
                  "layout_height_chars", "x_density", "y_density", "x_shift", 
                  "y_shift", "y_tolerance", "use_text_flow", "presorted", "expand_ligatures"]
WORD_EXTRACTOR_KWARGS = ["x_tolerance", "y_tolerance", "keep_blank_chars", "use_text_flow",
                         "horizontal_ltr", "vertical_ttb", "extra_attrs", "split_at_punctuation", 
                         "expand_ligatures"]


def chars_to_textmap(chars: list, **kwargs) -> TextMap:
    kwargs.update({"presorted": True})

    extractor = WordExtractor(
        **{k: kwargs[k] for k in WORD_EXTRACTOR_KWARGS if k in kwargs}
    )
    wordmap = extractor.extract_wordmap(chars)
    textmap = wordmap.to_textmap(
        **{k: kwargs[k] for k in TEXTMAP_KWARGS if k in kwargs}
    )

    return textmap


def extract_text(chars: list, **kwargs) -> str:
    chars = to_list(chars)
    if len(chars) == 0:
        return ""

    if kwargs.get("layout"):
        textmap = chars_to_textmap(chars, **kwargs)
        return textmap.as_string
    else:
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


def dedupe_chars(chars: list, tolerance=1) -> list:
    """
    Removes duplicate chars — those sharing the same text, fontname, size,
    and positioning (within `tolerance`) as other characters in the set.
    """
    key = itemgetter("fontname", "size", "upright", "text")
    pos_key = itemgetter("doctop", "x0")

    def yield_unique_chars(chars: list):
        sorted_chars = sorted(chars, key=key)
        for group_key, group_chars in itertools.groupby(sorted_chars, key=key):
            group_chars_list = list(group_chars)
            if len(group_chars_list) == 1:
                yield group_chars_list[0]
            else:
                sorted_group = sorted(group_chars_list, key=pos_key)
                yield sorted_group[0]
                
                for char in sorted_group[1:]:
                    prev_char = sorted_group[sorted_group.index(char) - 1]
                    if (
                        abs(char["doctop"] - prev_char["doctop"]) > tolerance
                        or abs(char["x0"] - prev_char["x0"]) > tolerance
                    ):
                        yield char

    deduped = yield_unique_chars(chars)
    return sorted(deduped, key=chars.index)


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
            "doctop": rect["doctop"] + rect["height"],
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


def curve_to_edges(curve) -> list:
    point_pairs = zip(curve["pts"], curve["pts"][1:])
    return [
        {
            "object_type": "curve_edge",
            "x0": min(p0[0], p1[0]),
            "x1": max(p0[0], p1[0]),
            "top": min(p0[1], p1[1]),
            "doctop": min(p0[1], p1[1]) + (curve["doctop"] - curve["top"]),
            "bottom": max(p0[1], p1[1]),
            "width": abs(p0[0] - p1[0]),
            "height": abs(p0[1] - p1[1]),
            "orientation": "v" if p0[0] == p1[0] else ("h" if p0[1] == p1[1] else None),
        }
        for p0, p1 in point_pairs
    ]


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


class GridReconstructor:
    """
    Reconstructs table grid structure using lines or text alignment strategies.
    """
    
    def __init__(self, page: fitz.Page, settings: GridReconstructionSettings = None):
        """
        Initialize the grid reconstructor.
        
        Args:
            page: PyMuPDF page object
            settings: Grid reconstruction settings
        """
        self.page = page
        self.settings = settings or GridReconstructionSettings()
    
    def reconstruct_grid_lines_strategy(self, table_bbox: BBox, 
                                       vector_graphics: List[VectorGraphic]) -> List[Cell]:
        """
        Reconstruct grid using the "lines" strategy - explicit vector graphics.
        
        This method identifies horizontal and vertical lines from vector graphics,
        finds their intersections, clusters them, and creates a grid of cells.
        
        Args:
            table_bbox: Bounding box of the table
            vector_graphics: List of vector graphic objects within the table
        
        Returns:
            List[Cell]: List of cell bounding boxes
        """
        x0, y0, x1, y1 = table_bbox
        tolerance = self.settings.snap_tolerance
        
        # Step 1: Identify horizontal and vertical lines
        horizontal_lines = []
        vertical_lines = []
        
        for graphic in vector_graphics:
            # Check if this is a line (height ≈ 0 for horizontal, width ≈ 0 for vertical)
            if graphic.get("type") == "line" or "rect" in graphic:
                g_bbox = graphic.get("bbox", graphic.get("rect", [0, 0, 0, 0]))
                gx0, gy0, gx1, gy1 = g_bbox
                
                # Horizontal line: height is very small
                if abs(gy1 - gy0) <= tolerance:
                    y_coord = (gy0 + gy1) / 2
                    if y0 <= y_coord <= y1:  # Within table bounds
                        horizontal_lines.append(y_coord)
                
                # Vertical line: width is very small
                if abs(gx1 - gx0) <= tolerance:
                    x_coord = (gx0 + gx1) / 2
                    if x0 <= x_coord <= x1:  # Within table bounds
                        vertical_lines.append(x_coord)
        
        # Step 2: Cluster intersection points
        clustered_h_lines = cluster_coordinates(horizontal_lines, tolerance)
        clustered_v_lines = cluster_coordinates(vertical_lines, tolerance)
        
        # Ensure table boundaries are included
        if not any(abs(line - y0) <= tolerance for line in clustered_h_lines):
            clustered_h_lines.append(y0)
        if not any(abs(line - y1) <= tolerance for line in clustered_h_lines):
            clustered_h_lines.append(y1)
        if not any(abs(line - x0) <= tolerance for line in clustered_v_lines):
            clustered_v_lines.append(x0)
        if not any(abs(line - x1) <= tolerance for line in clustered_v_lines):
            clustered_v_lines.append(x1)
        
        clustered_h_lines = sorted(clustered_h_lines)
        clustered_v_lines = sorted(clustered_v_lines)
        
        # Step 3: Create grid cells
        cells = []
        for i in range(len(clustered_h_lines) - 1):
            for j in range(len(clustered_v_lines) - 1):
                cell = (
                    clustered_v_lines[j],      # x0
                    clustered_h_lines[i],      # y0
                    clustered_v_lines[j + 1],  # x1
                    clustered_h_lines[i + 1]   # y1
                )
                cells.append(cell)
        
        return cells
    
    def reconstruct_grid_text_strategy(self, table_bbox: BBox, 
                                      characters: List[Character]) -> List[Cell]:
        """
        Reconstruct grid using the "text" strategy - text alignment and whitespace.
        
        This method groups characters into words and lines, then infers column
        boundaries based on text alignment patterns.
        
        Args:
            table_bbox: Bounding box of the table
            characters: List of character objects within the table
        
        Returns:
            List[Cell]: List of cell bounding boxes
        """
        if not characters:
            return []
        
        tolerance = self.settings.line_tolerance
        x0, y0, x1, y1 = table_bbox
        
        # Step 1: Group characters into words and lines
        words = self._group_chars_to_words(characters)
        lines = self._group_words_to_lines(words, tolerance)
        
        # Step 2: Infer column boundaries from text alignment
        column_boundaries = set([x0, x1])  # Always include table edges
        
        for line in lines:
            for word in line:
                # Add right edge of each word as potential column boundary
                column_boundaries.add(word["x1"])
                column_boundaries.add(word["x0"])  # Also left edge
        
        # Cluster column boundaries
        clustered_columns = cluster_coordinates(list(column_boundaries), 
                                              self.settings.snap_tolerance)
        clustered_columns = sorted(clustered_columns)
        
        # Step 3: Create horizontal boundaries from line positions
        line_boundaries = [y0, y1]  # Table edges
        for line in lines:
            if line:  # Non-empty line
                avg_top = sum(word["top"] for word in line) / len(line)
                avg_bottom = sum(word["bottom"] for word in line) / len(line)
                line_boundaries.extend([avg_top, avg_bottom])
        
        clustered_rows = cluster_coordinates(line_boundaries, tolerance)
        clustered_rows = sorted(clustered_rows)
        
        # Step 4: Create grid cells
        cells = []
        for i in range(len(clustered_rows) - 1):
            for j in range(len(clustered_columns) - 1):
                cell = (
                    clustered_columns[j],      # x0
                    clustered_rows[i],         # y0
                    clustered_columns[j + 1],  # x1
                    clustered_rows[i + 1]      # y1
                )
                cells.append(cell)
        
        return cells
    
    def _group_chars_to_words(self, characters: List[Character]) -> List[Dict]:
        """Group characters into words based on proximity."""
        if not characters:
            return []
        
        # Sort characters by position
        sorted_chars = sorted(characters, key=lambda c: (c["top"], c["x0"]))
        
        words = []
        current_word_chars = [sorted_chars[0]]
        
        for char in sorted_chars[1:]:
            # Check if this character belongs to the current word
            last_char = current_word_chars[-1]
            
            # Same line and close horizontally
            if (abs(char["top"] - last_char["top"]) <= 2 and 
                char["x0"] - last_char["x1"] <= 3):
                current_word_chars.append(char)
            else:
                # Finish current word
                if current_word_chars:
                    word = self._chars_to_word(current_word_chars)
                    words.append(word)
                current_word_chars = [char]
        
        # Don't forget the last word
        if current_word_chars:
            word = self._chars_to_word(current_word_chars)
            words.append(word)
        
        return words
    
    def _chars_to_word(self, chars: List[Character]) -> Dict:
        """Convert a list of characters to a word dictionary."""
        if not chars:
            return {}
        
        return {
            "x0": min(c["x0"] for c in chars),
            "x1": max(c["x1"] for c in chars),
            "top": min(c["top"] for c in chars),
            "bottom": max(c["bottom"] for c in chars),
            "text": "".join(c.get("c", "") for c in chars)
        }
    
    def _group_words_to_lines(self, words: List[Dict], tolerance: float) -> List[List[Dict]]:
        """Group words into lines based on vertical alignment."""
        if not words:
            return []
        
        # Sort words by vertical position
        sorted_words = sorted(words, key=lambda w: w["top"])
        
        lines = []
        current_line = [sorted_words[0]]
        
        for word in sorted_words[1:]:
            # Check if this word belongs to current line
            if abs(word["top"] - current_line[-1]["top"]) <= tolerance:
                current_line.append(word)
            else:
                # Start new line
                lines.append(sorted(current_line, key=lambda w: w["x0"]))
                current_line = [word]
        
        # Don't forget the last line
        if current_line:
            lines.append(sorted(current_line, key=lambda w: w["x0"]))
        
        return lines


class CellGroup:
    """Base class for grouped table cells."""
    
    def __init__(self, cells: List[Cell]):
        """
        Initialize cell group.
        
        Args:
            cells: List of cell bounding boxes
        """
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
    """Represents table header information."""
    
    def __init__(self, bbox: BBox, cells: List[Cell], names: List[str], external: bool):
        """
        Initialize table header.
        
        Args:
            bbox: Bounding box of the header
            cells: List of header cell bounding boxes
            names: List of column names
            external: Whether header is external to table body
        """
        self.bbox = bbox
        self.cells = cells
        self.names = names
        self.external = external


class Table:
    """
    Represents a complete table structure with cells, rows, and content extraction.
    """
    
    def __init__(self, page: fitz.Page, cells: List[Cell]):
        """
        Initialize table.
        
        Args:
            page: PyMuPDF page object
            cells: List of cell bounding boxes
        """
        self.page = weakref.proxy(page)
        self.cells = cells
        self._header = None
    
    @property
    def bbox(self) -> BBox:
        """Calculate overall table bounding box."""
        if not self.cells:
            return (0, 0, 0, 0)
        
        valid_cells = [c for c in self.cells if c is not None]
        if not valid_cells:
            return (0, 0, 0, 0)
        
        return (
            min(c[0] for c in valid_cells),
            min(c[1] for c in valid_cells),
            max(c[2] for c in valid_cells),
            max(c[3] for c in valid_cells)
        )
    
    @property
    def rows(self) -> List[TableRow]:
        """Get table rows organized by vertical position."""
        if not self.cells:
            return []
        
        # Sort cells by y-coordinate (top), then x-coordinate (left)
        sorted_cells = sorted(self.cells, key=lambda c: (c[1], c[0]) if c else (float('inf'), float('inf')))
        
        # Get unique x-coordinates to determine column structure
        x_coords = sorted(set(c[0] for c in self.cells if c is not None))
        
        # Group cells by row (same y-coordinate)
        rows = []
        for y, row_cells in itertools.groupby(sorted_cells, key=lambda c: c[1] if c else None):
            if y is None:
                continue
            
            # Create dictionary mapping x-coordinate to cell
            x_to_cell = {cell[0]: cell for cell in row_cells if cell is not None}
            
            # Create row with cells in column order
            row_cells_ordered = [x_to_cell.get(x) for x in x_coords]
            row = TableRow(row_cells_ordered)
            rows.append(row)
        
        return rows
    
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
            self._header = self._detect_header()
        return self._header
    
    def extract(self, **kwargs) -> List[List[str]]:
        """
        Extract text content from all table cells.
        
        Args:
            **kwargs: Additional extraction options
        
        Returns:
            List[List[str]]: 2D array of cell text content
        """
        # Get all characters on the page
        try:
            text_dict = self.page.get_text("dict")
            chars = []
            for block in text_dict["blocks"]:
                if "lines" in block:  # Text block
                    for line in block["lines"]:
                        for span in line["spans"]:
                            for char in span.get("chars", []):
                                chars.append(char)
        except:
            chars = []
        
        table_array = []
        
        for row in self.rows:
            row_array = []
            
            # Get characters within this row
            if chars and row.bbox:
                row_chars = [c for c in chars if char_in_bbox(c, row.bbox)]
            else:
                row_chars = []
            
            for cell in row.cells:
                if cell is None:
                    cell_text = ""
                else:
                    # Get characters within this cell
                    cell_chars = [c for c in row_chars if char_in_bbox(c, cell)]
                    
                    if cell_chars:
                        cell_text = extract_text_from_chars(cell_chars, **kwargs)
                    else:
                        cell_text = ""
                
                row_array.append(cell_text)
            
            table_array.append(row_array)
        
        return table_array
    
    def to_markdown(self, clean: bool = False, fill_empty: bool = True) -> str:
        """
        Convert table to markdown format.
        
        Args:
            clean: Whether to clean markdown syntax from content
            fill_empty: Whether to fill empty cells with neighboring values
        
        Returns:
            str: Markdown representation of the table
        """
        extracted_data = self.extract()
        
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
                name = html.escape(name.replace("-", "&#45;"))
            
            output += name + "|"
        
        output += "\n"
        
        # Separator row
        output += "|" + "|".join("---" for _ in range(col_count)) + "|\n"
        
        # Data rows (skip first row if header is internal)
        start_row = 1 if (self.header and not self.header.external) else 0
        
        for row_data in extracted_data[start_row:]:
            line = "|"
            for i, cell_text in enumerate(row_data[:col_count]):
                if cell_text is None:
                    cell_text = ""
                
                cell_text = str(cell_text).replace("\n", "<br>")
                if clean:
                    cell_text = html.escape(cell_text.replace("-", "&#45;"))
                
                line += cell_text + "|"
            
            output += line + "\n"
        
        return output
    
    def to_pandas(self):
        """
        Convert table to pandas DataFrame.
        
        Returns:
            pandas.DataFrame: DataFrame representation of the table
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
    
    def _detect_header(self) -> TableHeader:
        """
        Detect table header information.
        
        Returns:
            TableHeader: Detected header information
        """
        if not self.rows:
            return TableHeader((0, 0, 0, 0), [], [], False)
        
        # Simple heuristic: use first row as header
        first_row = self.rows[0]
        header_names = []
        
        # Extract text from first row cells
        first_row_data = self.extract()[0] if self.extract() else []
        
        for i, cell_text in enumerate(first_row_data):
            name = str(cell_text).strip() if cell_text else f"Col{i+1}"
            header_names.append(name)
        
        return TableHeader(
            bbox=first_row.bbox,
            cells=first_row.cells,
            names=header_names,
            external=False  # Assuming header is part of table
        )
    
    def _fill_empty_cells(self, data: List[List[str]]) -> List[List[str]]:
        """Fill empty cells with neighboring values."""
        if not data:
            return data
        
        filled_data = [row[:] for row in data]  # Deep copy
        
        # Fill horizontally (left to right within rows)
        for i, row in enumerate(filled_data):
            for j in range(len(row) - 1):
                if not row[j + 1] or not row[j + 1].strip():
                    filled_data[i][j + 1] = row[j]
        
        # Fill vertically (top to bottom within columns)
        for j in range(len(filled_data[0]) if filled_data else 0):
            for i in range(len(filled_data) - 1):
                if (i + 1 < len(filled_data) and 
                    j < len(filled_data[i + 1]) and
                    (not filled_data[i + 1][j] or not filled_data[i + 1][j].strip())):
                    filled_data[i + 1][j] = filled_data[i][j]
        
        return filled_data


class TableProcessor:
    """
    Main table processing system that converts bounding boxes to structured tables.
    """
    
    def __init__(self, page: fitz.Page, 
                 reconstruction_settings: GridReconstructionSettings = None):
        """
        Initialize the table processor.
        
        Args:
            page: PyMuPDF page object
            reconstruction_settings: Settings for grid reconstruction
        """
        self.page = page
        self.settings = reconstruction_settings or GridReconstructionSettings()
        self.grid_reconstructor = GridReconstructor(page, self.settings)
    
    def process_tables(self, table_bboxes: List[BBox], 
                      strategy: str = "lines") -> List[Table]:
        """
        Process a list of table bounding boxes into structured Table objects.
        
        This is the main entry point that implements the complete algorithm:
        1. Extract content within each bounding box
        2. Reconstruct grid structure using specified strategy
        3. Populate cells with content
        4. Create Table objects
        
        Args:
            table_bboxes: List of table bounding boxes [(x0, y0, x1, y1), ...]
            strategy: Grid reconstruction strategy ("lines" or "text")
        
        Returns:
            List[Table]: List of processed Table objects
        """
        tables = []
        
        for table_bbox in table_bboxes:
            try:
                # Step 1: Extract content within bounding box
                characters, vector_graphics = self._extract_content_in_bbox(table_bbox)
                
                # Step 2: Reconstruct grid structure
                if strategy == "lines":
                    cells = self.grid_reconstructor.reconstruct_grid_lines_strategy(
                        table_bbox, vector_graphics
                    )
                elif strategy == "text":
                    cells = self.grid_reconstructor.reconstruct_grid_text_strategy(
                        table_bbox, characters
                    )
                else:
                    raise ValueError(f"Unknown strategy: {strategy}")
                
                # Step 3: Create Table object (cell population happens in Table.extract())
                if cells:  # Only create table if cells were found
                    table = Table(self.page, cells)
                    tables.append(table)
            
            except Exception as e:
                # Log error but continue processing other tables
                print(f"Error processing table {table_bbox}: {e}")
                continue
        
        return tables
    
    def _extract_content_in_bbox(self, table_bbox: BBox) -> Tuple[List[Character], 
                                                                 List[VectorGraphic]]:
        """
        Extract all content (text characters and vector graphics) within a bounding box.
        
        This implements the mathematical intersection condition:
        (c_x0 < x1) ∧ (c_x1 > x0) ∧ (c_y0 < y1) ∧ (c_y1 > y0)
        
        Args:
            table_bbox: Bounding box (x0, y0, x1, y1)
        
        Returns:
            Tuple[List[Character], List[VectorGraphic]]: Characters and vector graphics
        """
        characters = []
        vector_graphics = []
        
        try:
            # Extract text characters
            text_dict = self.page.get_text("dict")
            for block in text_dict["blocks"]:
                if "lines" in block:  # Text block
                    for line in block["lines"]:
                        for span in line["spans"]:
                            for char in span.get("chars", []):
                                char_bbox = (char["bbox"][0], char["bbox"][1], 
                                           char["bbox"][2], char["bbox"][3])
                                if bbox_intersects(char_bbox, table_bbox):
                                    characters.append(char)
            
            # Extract vector graphics (drawings, lines, rectangles)
            drawings = self.page.get_drawings()
            for drawing in drawings:
                drawing_bbox = drawing.get("rect", (0, 0, 0, 0))
                if bbox_intersects(drawing_bbox, table_bbox):
                    vector_graphics.append(drawing)
        
        except Exception as e:
            print(f"Error extracting content from bbox {table_bbox}: {e}")
        
        return characters, vector_graphics


# Utility functions for advanced table processing

def merge_overlapping_tables(tables: List[Table], 
                           overlap_threshold: float = 0.1) -> List[Table]:
    """
    Merge tables that significantly overlap with each other.
    
    Args:
        tables: List of Table objects
        overlap_threshold: Minimum overlap ratio to trigger merge
    
    Returns:
        List[Table]: List of merged tables
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
            merged_cells = []
            for table in tables_to_merge:
                merged_cells.extend(table.cells)
            
            merged_table = Table(table1.page, merged_cells)
            merged_tables.append(merged_table)
    
    return merged_tables


def filter_small_tables(tables: List[Table], 
                       min_rows: int = 2, 
                       min_cols: int = 2) -> List[Table]:
    """
    Filter out tables that are too small to be meaningful.
    
    Args:
        tables: List of Table objects
        min_rows: Minimum number of rows
        min_cols: Minimum number of columns
    
    Returns:
        List[Table]: Filtered list of tables
    """
    return [table for table in tables 
            if table.row_count >= min_rows and table.col_count >= min_cols]


def sort_tables_by_position(tables: List[Table]) -> List[Table]:
    """
    Sort tables by their position on the page (top-to-bottom, left-to-right).
    
    Args:
        tables: List of Table objects
    
    Returns:
        List[Table]: Sorted list of tables
    """
    return sorted(tables, key=lambda t: (t.bbox[1], t.bbox[0]))


# Example usage and demonstration
def demonstrate_table_processing():
    """
    Demonstrate the table processing system with example usage.
    """
    # Example bounding boxes for tables (in practice, these would come from 
    # a table detection system like YOLO, Detectron2, etc.)
    example_table_bboxes = [
        (100, 200, 400, 350),  # Table 1: x0=100, y0=200, x1=400, y1=350
        (450, 200, 700, 400),  # Table 2: x0=450, y0=200, x1=700, y1=400
        (100, 450, 500, 600),  # Table 3: x0=100, y0=450, x1=500, y1=600
    ]
    
    # This is how you would use the system:
    """
    # Open PDF document
    doc = fitz.open("document.pdf")
    page = doc[0]  # First page
    
    # Initialize table processor
    settings = GridReconstructionSettings(
        snap_tolerance=3.0,
        join_tolerance=3.0,
        intersection_tolerance=3.0
    )
    processor = TableProcessor(page, settings)
    
    # Process tables using detected bounding boxes
    tables = processor.process_tables(example_table_bboxes, strategy="lines")
    
    # Post-process results
    tables = filter_small_tables(tables, min_rows=2, min_cols=2)
    tables = merge_overlapping_tables(tables, overlap_threshold=0.1)
    tables = sort_tables_by_position(tables)
    
    # Extract and use table data
    for i, table in enumerate(tables):
        print(f"Table {i + 1}:")
        print(f"  Dimensions: {table.row_count} rows × {table.col_count} cols")
        print(f"  Bounding box: {table.bbox}")
        
        # Get as 2D array
        data_array = table.extract()
        print(f"  Data preview: {data_array[:2]}")  # First 2 rows
        
        # Get as markdown
        markdown = table.to_markdown()
        print(f"  Markdown length: {len(markdown)} characters")
        
        # Get as pandas DataFrame (if pandas is available)
        try:
            df = table.to_pandas()
            print(f"  DataFrame shape: {df.shape}")
        except ImportError:
            print("  pandas not available for DataFrame conversion")
        
        print()
    
    doc.close()
    """


if __name__ == "__main__":
    """
    Example usage of the table processing system.
    
    To use this system:
    1. Install PyMuPDF: pip install pymupdf
    2. Optionally install pandas: pip install pandas
    3. Provide table bounding boxes from your detection system
    4. Process tables using the TableProcessor class
    """
    
    print("Table Processing System")
    print("======================")
    print()
    print("This system implements a comprehensive table processing pipeline")
    print("that converts detected table bounding boxes into structured data.")
    print()
    print("Key features:")
    print("- Mathematical grid reconstruction using lines or text alignment")
    print("- Robust content extraction with intersection algorithms")
    print("- Multiple output formats: 2D arrays, markdown, pandas DataFrames")
    print("- Header detection and cell merging support")
    print("- Configurable processing parameters")
    print()
    print("See the demonstrate_table_processing() function for usage examples.")
    
    # Run demonstration
    demonstrate_table_processing()