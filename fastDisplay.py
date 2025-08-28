import os
import sys
import glob, shutil, getpass, socket, platform
import json
import numpy as np
from PIL import Image
import hashlib
import matplotlib
matplotlib.use('Qt5Agg', force=True)
matplotlib.interactive(False)
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QTabWidget, QLabel, QPushButton, QSpinBox, QComboBox, QCheckBox,
    QRadioButton, QGroupBox, QScrollArea, QTextEdit, QSlider, QLineEdit,
    QFileDialog, QMessageBox, QDoubleSpinBox, QFormLayout, QTabBar, QButtonGroup, QTreeWidget, QTreeWidgetItem
)
from PyQt5.QtCore import Qt, QTimer, QRect, QRectF, QPoint, QPointF, QProcess, pyqtSignal,QPropertyAnimation,QEvent
from PyQt5.QtGui import QImage, QPixmap, QPainter, QPainterPath,QPen,QCursor,QColor,QTextCursor,QKeySequence
from PyQt5.QtWidgets import QDialog, QStyle,QShortcut
import math
import gc
import shlex
try:
    import cv2
except ImportError:
    cv2 = None
from Live_Display_AppVZ import VideoModeHandler
# ===== WGS84 constants =====
a = 6378137.0          # semi-major axis (meters)
b = 6356752.3142       # semi-minor axis (meters)
e2 = (a**2 - b**2) / a**2  # eccentricity squared

def meters_per_degree(lat_deg):
    """Return meters per degree latitude and longitude at given latitude."""
    lat_rad = math.radians(lat_deg)
    m_per_deg_lat = (math.pi / 180) * a * (1 - e2) / (1 - e2 * math.sin(lat_rad)**2)**1.5
    m_per_deg_lon = (math.pi / 180) * a * math.cos(lat_rad) / math.sqrt(1 - e2 * math.sin(lat_rad)**2)
    return m_per_deg_lat, m_per_deg_lon

def calculate_pixel_coordinates(center_lat, center_lon, width, height, pixel_size_m=1.5):
    """Calculate latitude and longitude for each pixel in the image."""
    # Center pixel indices
    center_x = width // 2
    center_y = height // 2

    # Accurate conversion factors at center latitude
    meters_per_deg_lat, meters_per_deg_lon = meters_per_degree(center_lat)

    # Create pixel coordinate grid
    x_indices = np.arange(width)
    y_indices = np.arange(height)
    X, Y = np.meshgrid(x_indices, y_indices)

    # Offset in meters relative to center pixel
    dx_m = (X - center_x) * pixel_size_m   # east (+) / west (-)
    dy_m = (center_y - Y) * pixel_size_m   # north (+) / south (-)

    # Convert to lat/lon degrees
    lat_grid = center_lat + (dy_m / meters_per_deg_lat)
    lon_grid = center_lon + (dx_m / meters_per_deg_lon)
    
    return lat_grid, lon_grid

def parse_meta_file(meta_file_path):
    """Robustly parse .meta file to extract latitude and longitude (decimal degrees).
    Returns (lat, lon) or (None, None) on failure."""
    center_lat = None
    center_lon = None
    try:
        with open(meta_file_path, 'r') as f:
            content = f.read()
        # accept forms like "Latitude: 12.345678" or "lat = 12.3456" (case-insensitive)
        mlat = re.search(r'(?i)(?:latitude|lat)\s*[:=]\s*([-+]?\d+(?:\.\d+)?)', content)
        mlon = re.search(r'(?i)(?:longitude|lon|long)\s*[:=]\s*([-+]?\d+(?:\.\d+)?)', content)
        if mlat:
            center_lat = float(mlat.group(1))
        if mlon:
            center_lon = float(mlon.group(1))
        return center_lat, center_lon
    except Exception as e:
        print(f"Error parsing meta file: {e}")
        return None, None


# ---------- Bit Depth Handling (unchanged) ----------
def unpack_8bit(data, w, h):
    try:
        return np.frombuffer(data, dtype=np.uint8).reshape((-1, h, w))
    except Exception as e:
        print(f"Error in unpack_8bit: {e}")
        return []

def unpack_10bit(data, w, h):
    total_pixels = w * h
    bytes_per_frame = (total_pixels * 10) // 8
    num_frames = len(data) // bytes_per_frame
    frames = []
    for i in range(num_frames):
        start = i * bytes_per_frame
        packed_data = data[start : start + bytes_per_frame]
        num_full_groups = len(packed_data) // 5
        d = np.frombuffer(packed_data[:num_full_groups*5], dtype=np.uint8).reshape(-1,5)
        expanded_data = np.zeros(d.shape[0], dtype=np.uint64)
        for j in range(5):
            expanded_data += d[:,j].astype(np.uint64) << (8 * j)
        unpacked = np.zeros(num_full_groups * 4, dtype=np.uint16)
        for j in range(4):
            unpacked[j::4] = (expanded_data >> (10 * j)) & 0x3FF
        remaining_bytes = len(packed_data) % 5
        if remaining_bytes:
            last_bits = int.from_bytes(packed_data[-remaining_bytes:], 'little')
            extra_pixels = np.array([(last_bits >> (10 * k)) & 0x3FF for k in range((remaining_bytes * 8) // 10)])
            unpacked = np.concatenate([unpacked, extra_pixels[:total_pixels - len(unpacked)]])
        if len(unpacked) < total_pixels:
            unpacked = np.pad(unpacked, (0, total_pixels - len(unpacked)), 'constant')
        scaled = np.clip((unpacked * 255.0 / 1023), 0, 255).astype(np.uint8)
        frames.append(scaled.reshape(h, w))
    return frames

def unpack_12bit(data, w, h):
    try:
        total_pixels = w * h
        frame_size = (total_pixels * 12) // 8
        num_frames = len(data) // frame_size
        frames = []
        for i in range(num_frames):
            chunk = data[i * frame_size:(i + 1) * frame_size]
            d = np.frombuffer(chunk, dtype=np.uint8)
            d = d[:(len(d) // 3) * 3].reshape(-1, 3)
            px0 = d[:, 0] + ((d[:, 1] & 0x0F) << 8)
            px1 = ((d[:, 1] >> 4) & 0x0F) + (d[:, 2] << 4)
            frame = np.empty((total_pixels,), dtype=np.uint16)
            frame[0::2] = px0
            frame[1::2] = px1
            frames.append(np.clip(frame / 16, 0, 255).astype(np.uint8).reshape(h, w))
        return frames
    except Exception as e:
        print(f"Error in unpack_12bit: {e}")
        return []

def unpack_by_bitdepth(data, w, h, bitdepth):
    if bitdepth == 8:
        return unpack_8bit(data, w, h)
    elif bitdepth == 10:
        return unpack_10bit(data, w, h)
    elif bitdepth == 12:
        return unpack_12bit(data, w, h)
    else:
        raise ValueError(f"Unsupported bit depth: {bitdepth}")

class LazyFrames:
    def __init__(self, file_path, w, h, bitdepth):
        self.file_path = file_path
        self.w = w
        self.h = h
        self.bitdepth = bitdepth
        total_pixels = w * h
        if bitdepth == 8:
            self.bytes_per_frame = total_pixels
        elif bitdepth == 10:
            self.bytes_per_frame = (total_pixels * 10) // 8
        elif bitdepth == 12:
            self.bytes_per_frame = (total_pixels * 12) // 8
        else:
            raise ValueError(f"Unsupported bit depth: {bitdepth}")
        self.num_frames = os.path.getsize(file_path) // self.bytes_per_frame if self.bytes_per_frame > 0 else 0

    def __len__(self):
        return self.num_frames

    def __getitem__(self, idx):
        if idx < 0 or idx >= self.num_frames:
            raise IndexError("Frame index out of range")
        start = idx * self.bytes_per_frame
        with open(self.file_path, 'rb') as f:
            f.seek(start)
            chunk = f.read(self.bytes_per_frame)
        frames = unpack_by_bitdepth(chunk, self.w, self.h, self.bitdepth)
        if not frames:
            return np.zeros((self.h, self.w), dtype=np.uint8)
        return frames[0]

class PixelInfoBox(QWidget):
    def __init__(self, parent=None, matrix_size_var=None, lat_lon_data=None):
        super().__init__(parent)
        self.matrix_size_var = matrix_size_var
        self.lat_lon_data = lat_lon_data

        # Floating/movable state
        self._is_floating = False
        self._drag_pos = None

        layout = QVBoxLayout()
        self.setLayout(layout)

        control_layout = QHBoxLayout()
        layout.addLayout(control_layout)

        control_layout.addWidget(QLabel("Matrix Size:"))
        self.size_combo = QComboBox()
        self.size_combo.addItems(["3", "5", "7", "9"])
        try:
            self.size_combo.setCurrentText(str(matrix_size_var.value()))
        except Exception:
            pass
        self.size_combo.currentTextChanged.connect(lambda v: self.matrix_size_var.setValue(int(v)))
        control_layout.addWidget(self.size_combo)

        layout.addWidget(QLabel("Pixel Info"))
        self.info_text = QTextEdit()
        self.info_text.setReadOnly(True)
        self.info_text.setFontFamily("Consolas")
        self.info_text.setFontPointSize(9)
        self.info_text.setFixedHeight(150)
        layout.addWidget(self.info_text)

        # Small visual handle / caption could be added here if desired
        # but dragging entire widget is simpler and works well.

    # --- Floating control API ---
    def make_floating(self, start_pos=None):
        """Detach and show as a top-most, frameless window (movable by dragging)."""
        try:
            self._is_floating = True
            self.setParent(None)
            flags = Qt.Window | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
            self.setWindowFlags(flags)
            # Optional: give a small border so users can see it
            self.setStyleSheet(self.styleSheet() + "QWidget{border:1px solid rgba(200,200,200,0.6); background: rgba(30,30,30,0.92); color: white;}")
            if start_pos:
                try:
                    self.move(start_pos)
                except Exception:
                    pass
            self.show()
            self.raise_()
        except Exception as e:
            print("make_floating error:", e)

    def make_embedded(self, parent_panel, insert_index=None):
        """Reparent back into the left panel layout (normal widget mode)."""
        try:
            self._is_floating = False
            self.hide()
            self.setParent(parent_panel)
            self.setWindowFlags(Qt.Widget)
            # reset any floating styles (optional)
            self.setStyleSheet("")
            # attach back to layout if available
            layout = getattr(parent_panel, "layout", None)
            if callable(layout):
                # if parent_panel.layout() exists use that
                pl = parent_panel.layout()
                if pl is not None:
                    if insert_index is not None and 0 <= insert_index < pl.count():
                        pl.insertWidget(insert_index, self)
                    else:
                        pl.addWidget(self)
            else:
                try:
                    parent_layout = parent_panel.layout()
                    if parent_layout is not None:
                        if insert_index is not None and 0 <= insert_index < parent_layout.count():
                            parent_layout.insertWidget(insert_index, self)
                        else:
                            parent_layout.addWidget(self)
                except Exception:
                    pass
            self.show()
        except Exception as e:
            print("make_embedded error:", e)

    # --- Mouse handlers to drag when floating ---
    def mousePressEvent(self, event):
        if self._is_floating and event.button() == Qt.LeftButton:
            self._drag_pos = event.globalPos() - self.frameGeometry().topLeft()
            event.accept()
        else:
            super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self._is_floating and self._drag_pos is not None:
            new_pos = event.globalPos() - self._drag_pos
            # keep widget inside screen roughly
            try:
                self.move(new_pos)
            except Exception:
                pass
            event.accept()
        else:
            super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        self._drag_pos = None
        super().mouseReleaseEvent(event)

    def update_info(self, x, y, values, is_rgb=False):
        # Debug log (kept from original)
        print(f"Updating PixelInfoBox: Position ({x}, {y}), is_rgb: {is_rgb}, matrix shape: {getattr(values, 'shape', None)}")

        # Clear UI
        self.info_text.clear()

        # Basic position + lat/lon (if available)
        self.info_text.append(f"Position: ({int(x)}, {int(y)})")  # Use absolute for Position

        lat_lon = getattr(self, "lat_lon_data", None)
        if lat_lon is not None:
            try:
                lat_grid, lon_grid = lat_lon
                lat_h, lat_w = lat_grid.shape[:2]
                ix = int(round(x))
                iy = int(round(y))
                if 0 <= iy < lat_h and 0 <= ix < lat_w:
                    lat = float(lat_grid[iy, ix])
                    lon = float(lon_grid[iy, ix])
                    self.info_text.append(f"Lat: {lat:.8f}, Lon: {lon:.8f}")
                else:
                    # Scaling fallback using passed full_width/full_height
                    sx = lat_w / full_width if full_width > 0 else 1.0
                    sy = lat_h / full_height if full_height > 0 else 1.0
                    ix = int(round(x * sx))
                    iy = int(round(y * sy))
                    if 0 <= iy < lat_h and 0 <= ix < lat_w:
                        lat = float(lat_grid[iy, ix])
                        lon = float(lon_grid[iy, ix])
                        self.info_text.append(f"Lat: {lat:.8f}, Lon: {lon:.8f}")
                    else:
                        self.info_text.append("Lat/Lon: (out of bounds)")
            except Exception as e:
                print("Error reading lat_lon_data:", e)
                self.info_text.append("Lat/Lon: N/A")
        else:
            self.info_text.append("Lat/Lon: N/A")

        # Pixel matrix header
        try:
            size = int(self.matrix_size_var.value())
            if size <= 0:
                size = 1
        except Exception:
            size = 1
        self.info_text.append(f"\n{size}x{size} Pixel Matrix:")

        # Helper to format a single value (safe)
        def _fmt_val(v):
            try:
                return f"{int(v):3}"
            except Exception:
                try:
                    return f"{float(v):3.0f}"
                except Exception:
                    return "  0"

        # If values is a numpy array, render matrix; otherwise show fallback
        if isinstance(values, np.ndarray):
            center_i = size // 2
            center_j = size // 2

            if is_rgb and values.ndim == 3:
                for i in range(size):
                    rline = "R: [ "
                    for j in range(size):
                        try:
                            val = values[i, j, 0] if i < values.shape[0] and j < values.shape[1] else 0
                        except Exception:
                            val = 0
                        if i == center_i and j == center_j:
                            rline += f"<span style='background-color:yellow'>{_fmt_val(val)}</span>"
                        else:
                            rline += _fmt_val(val)
                        if j < size - 1:
                            rline += " "
                    rline += " ] "

                    gline = "G: [ "
                    for j in range(size):
                        try:
                            val = values[i, j, 1] if i < values.shape[0] and j < values.shape[1] else 0
                        except Exception:
                            val = 0
                        if i == center_i and j == center_j:
                            gline += f"<span style='background-color:yellow'>{_fmt_val(val)}</span>"
                        else:
                            gline += _fmt_val(val)
                        if j < size - 1:
                            gline += " "
                    gline += " ] "

                    bline = "B: [ "
                    for j in range(size):
                        try:
                            val = values[i, j, 2] if i < values.shape[0] and j < values.shape[1] else 0
                        except Exception:
                            val = 0
                        if i == center_i and j == center_j:
                            bline += f"<span style='background-color:yellow'>{_fmt_val(val)}</span>"
                        else:
                            bline += _fmt_val(val)
                        if j < size - 1:
                            bline += " "
                    bline += " ]"

                    self.info_text.append(rline + gline + bline)
            else:
                for i in range(size):
                    line = "    [ "
                    for j in range(size):
                        try:
                            val = values[i, j] if i < values.shape[0] and j < values.shape[1] else 0
                        except Exception:
                            val = 0
                        if i == center_i and j == center_j:
                            line += f"<span style='background-color:yellow'>{_fmt_val(val)}</span>"
                        else:
                            line += _fmt_val(val)
                        if j < size - 1:
                            line += " "
                    line += " ]"
                    self.info_text.append(line)

            try:
                self.info_text.repaint()
            except Exception:
                try:
                    self.info_text.update()
                except Exception:
                    pass
        else:
            self.info_text.append("Value: Unknown")
            print("No valid pixel data provided")

        # NEW: Ensure the box is raised and visible after update if floating (fixes disappearance on click in fullscreen)
        if self._is_floating:
            self.show()
            self.raise_()

class TerminalWidget(QWidget):
    """
    Simple terminal: runs one-off commands via QProcess (captures stdout+stderr).
    Uses maximumHeight=0 as closed state so parent can animate it.
    """
    output_signal = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        # Output area
        self.terminal_output = QTextEdit()
        self.terminal_output.setReadOnly(True)
        try:
            f = self.terminal_output.font()
            f.setFamily("Courier New")
            f.setPointSize(10)
            self.terminal_output.setFont(f)
        except Exception:
            pass
        layout.addWidget(self.terminal_output, 1)

        # Input line
        self.command_input = QLineEdit()
        self.command_input.setPlaceholderText("Type command and press Enter")
        self.command_input.returnPressed.connect(self.run_command)
        layout.addWidget(self.command_input)

        self.output_signal.connect(self.append_output)

        # choose default shell best-effort
        sys_platform = platform.system().lower()
        if 'windows' in sys_platform:
            self._default_shell = shutil.which("powershell.exe") or shutil.which("cmd.exe") or "cmd.exe"
        else:
            self._default_shell = shutil.which("bash") or shutil.which("sh") or "/bin/sh"

        # start closed
        self.setMaximumHeight(0)
        self.hide()

    def run_command(self):
        cmd = self.command_input.text().strip()
        if not cmd:
            return
        self.append_output(f"$ {cmd}\n")

        proc = QProcess(self)
        proc.setProcessChannelMode(QProcess.MergedChannels)

        def on_ready_read():
            try:
                data = proc.readAll().data().decode(errors="ignore")
            except Exception:
                data = ""
            if data:
                self.output_signal.emit(data)

        def on_finished(exit_code, exit_status):
            try:
                proc.readyRead.disconnect(on_ready_read)
            except Exception:
                pass
            try:
                proc.finished.disconnect(on_finished)
            except Exception:
                pass
            proc.deleteLater()

        proc.readyRead.connect(on_ready_read)
        proc.finished.connect(on_finished)

        if isinstance(self._default_shell, str) and 'cmd.exe' in self._default_shell.lower():
            proc.start(self._default_shell, ["/C", cmd])
        else:
            proc.start(self._default_shell, ["-c", cmd])

        self.command_input.clear()

    def append_output(self, text):
        self.terminal_output.moveCursor(QTextCursor.End)
        self.terminal_output.insertPlainText(text)
        self.terminal_output.moveCursor(QTextCursor.End)


    def run_command(self):
        cmd = self.command_input.text()
        if not cmd:
            return
        # echo the command to terminal output
        self.append_output(f"$ {cmd}\n")
        try:
            # write command to process stdin (use OS newline)
            self.process.write((cmd + os.linesep).encode())
        except Exception as e:
            self.append_output(f"[Failed to send command: {e}]\n")
        self.command_input.clear()

    def handle_stdout(self):
        try:
            data = self.process.readAllStandardOutput().data().decode(errors="ignore")
            if data:
                self.output_signal.emit(data)
        except Exception:
            pass

    def handle_stderr(self):
        try:
            data = self.process.readAllStandardError().data().decode(errors="ignore")
            if data:
                self.output_signal.emit(data)
        except Exception:
            pass

    def append_output(self, text):
        self.terminal_output.insertPlainText(text)
        self.terminal_output.moveCursor(QTextCursor.End)


class ImageLabel(QLabel):
    def __init__(self, parent=None,lat_lon_data=None):
        super().__init__(parent)
        self.lat_lon_data = lat_lon_data
        self.selection_pos = None
        self.zoom = 1.0
        self.full_width = 0
        self.full_height = 0
        self.parent_viewer = parent  # Store reference to ZoomableImageViewer
        self.magnifier_enabled = False
        self.magnifier_center = None  # QPoint in image coordinates
        self.magnifier_radius = 100  # in screen pixels
        self.magnifier_zoom = 8  # magnification factor
        self.resize_handle_size = 10  # Size of the resize handle (square)
        self.setMouseTracking(True)  # Enable cursor tracking for cursor changes
        self.is_fullscreen = False

    def resizeEvent(self, event):
        super().resizeEvent(event)
        

    def paintEvent(self, event):
        # Draw image first
        super().paintEvent(event)

        if not (self.magnifier_enabled and self.parent_viewer.original_image_data is not None):
            return

        painter = QPainter(self)
        try:
            painter.setRenderHint(QPainter.Antialiasing)

            # Get scroll and viewport info
            scroll_x = self.parent_viewer.scroll_area.horizontalScrollBar().value()
            scroll_y = self.parent_viewer.scroll_area.verticalScrollBar().value()

            pixmap = self.pixmap()
            if pixmap is None:
                return
            pixmap_w = pixmap.width()
            pixmap_h = pixmap.height()

            # Convert magnifier center (image coordinates) to label coordinates
            center_label = QPointF(
                self.magnifier_center.x() * self.parent_viewer.zoom - scroll_x,
                self.magnifier_center.y() * self.parent_viewer.zoom - scroll_y
            )

            radius = self.magnifier_radius
            mag_zoom = self.magnifier_zoom

            # Extract raw pixels from original image
            source_half = radius / mag_zoom
            x0 = max(0, int(self.magnifier_center.x() - source_half))
            x1 = min(self.parent_viewer.full_width, int(self.magnifier_center.x() + source_half))
            y0 = max(0, int(self.magnifier_center.y() - source_half))
            y1 = min(self.parent_viewer.full_height, int(self.magnifier_center.y() + source_half))

            sub_array = self.parent_viewer.original_image_data[y0:y1, x0:x1]

            if sub_array.size == 0:
                return

            # Create QImage from raw sub-array
            if sub_array.ndim == 3:  # RGB
                h, w, ch = sub_array.shape
                qimg = QImage(
                    sub_array.astype(np.uint8).tobytes(),
                    w, h, ch * w,
                    QImage.Format_RGB888
                )
            else:  # Grayscale
                h, w = sub_array.shape
                qimg = QImage(
                    sub_array.astype(np.uint8).tobytes(),
                    w, h, w,
                    QImage.Format_Grayscale8
                )

            # Scale with nearest neighbor (blocky pixels)
            scaled_img = qimg.scaled(
                int(w * mag_zoom),
                int(h * mag_zoom),
                Qt.KeepAspectRatio,
                Qt.FastTransformation  # nearest neighbor
            )
            pixmap = QPixmap.fromImage(scaled_img)

            # Draw circular magnifier
            path = QPainterPath()
            path.addEllipse(center_label, radius, radius)
            painter.setClipPath(path)
            painter.drawPixmap(
                QRectF(center_label.x() - radius, center_label.y() - radius, 2 * radius, 2 * radius),
                pixmap,
                QRectF(0, 0, pixmap.width(), pixmap.height())
            )

            # Draw border
            painter.setClipping(False)

            # Outer white border (thicker for glow/visibility on dark)
            pen = QPen(Qt.white, 4)
            painter.setPen(pen)
            painter.drawEllipse(center_label, radius, radius)

            # Inner black border (thinner for contrast on light)
            pen = QPen(Qt.black, 2)
            painter.setPen(pen)
            painter.drawEllipse(center_label, radius, radius)

            # Draw semi-transparent crosshair (+) (NEW: white instead of black for better visibility)
            pen = QPen(QColor(255, 255, 255, 128), 2)  # Semi-transparent white
            painter.setPen(pen)
            painter.drawLine(
                QPointF(center_label.x() - 10, center_label.y()),
                QPointF(center_label.x() + 10, center_label.y())
            )
            painter.drawLine(
                QPointF(center_label.x(), center_label.y() - 10),
                QPointF(center_label.x(), center_label.y() + 10)
            )

        finally:
            painter.end()
            gc.collect()

    def toggle_fullscreen(self):
        self.is_fullscreen = not self.is_fullscreen
        parent_app = self.parent_viewer
        while parent_app and not hasattr(parent_app, 'left_scroll'):
            parent_app = parent_app.parent()
        if not parent_app:
            print("Error: Could not find main app for full-screen toggle")
            return

        if self.is_fullscreen:
            parent_app._saved_ui_state = {
                'left_scroll_visible': parent_app.left_scroll.isVisible(),
                'left_scroll_width': parent_app.left_scroll.width(),
                'pixel_info_parent': parent_app.pixel_info_box.parent(),
                'pixel_info_index': None
            }
            try:
                if hasattr(parent_app, 'left_layout'):
                    parent_app._saved_ui_state['pixel_info_index'] = parent_app.left_layout.indexOf(parent_app.pixel_info_box)
            except Exception:
                pass

            try:
                parent_app.left_scroll.hide()
            except Exception:
                pass

            # Reparent pixel_info_box as floating with correct flags
            try:
                parent_app.pixel_info_box.setParent(None)
                flags = Qt.Window | Qt.WindowStaysOnTopHint | Qt.FramelessWindowHint
                parent_app.pixel_info_box.setWindowFlags(flags)
                parent_app.pixel_info_box.move(10, 10)
                parent_app.pixel_info_box.show()
                parent_app.pixel_info_box.raise_()
                QTimer.singleShot(0, parent_app.pixel_info_box.activateWindow)
            except Exception as e:
                print(f"Error reparenting pixel_info_box for fullscreen: {e}")

            try:
                self.parent_viewer.magnifier_toggle.show()
                self.parent_viewer.magnifier_zoom_slider.show()
            except Exception:
                pass

            try:
                self.parent_viewer.window().showFullScreen()
            except Exception as e:
                print(f"Error entering fullscreen: {e}")
        else:
            # Restore UI state
            try:
                if hasattr(parent_app, '_saved_ui_state'):
                    state = parent_app._saved_ui_state
                    if state.get('left_scroll_visible', True):
                        parent_app.left_scroll.show()
                    parent_app.left_scroll.setFixedWidth(state.get('left_scroll_width', parent_app.left_scroll.width()))

                    # Restore pixel_info_box
                    parent_app.pixel_info_box.setParent(parent_app.left_panel)
                    parent_app.pixel_info_box.setWindowFlags(Qt.Widget)
                    if hasattr(parent_app, 'left_layout'):
                        idx = state.get('pixel_info_index', parent_app.left_layout.count())
                        parent_app.left_layout.insertWidget(idx, parent_app.pixel_info_box)
                    parent_app.pixel_info_box.show()

                    del parent_app._saved_ui_state
            except Exception as e:
                print(f"Error restoring UI after fullscreen: {e}")

            try:
                self.parent_viewer.window().showNormal()
            except Exception as e:
                print(f"Error exiting fullscreen: {e}")


    def mousePressEvent(self, event):
        scroll_x = self.parent_viewer.scroll_area.horizontalScrollBar().value()
        scroll_y = self.parent_viewer.scroll_area.verticalScrollBar().value()
        mouse_label = event.pos() + QPoint(scroll_x, scroll_y)
        x = int((event.pos().x() + scroll_x) / self.parent_viewer.zoom)
        y = int((event.pos().y() + scroll_y) / self.parent_viewer.zoom)

        if event.button() == Qt.LeftButton:
            # Always emit pixel info on left click
            self.parent_viewer._emit_pixel_info_at(x, y)
            self.parent_viewer.selection_pos = (x, y)
            self.update()

            if self.magnifier_enabled:
                center_label = QPoint(
                    int(self.magnifier_center.x() * self.parent_viewer.zoom),
                    int(self.magnifier_center.y() * self.parent_viewer.zoom)
                )
                dx = mouse_label.x() - center_label.x()
                dy = mouse_label.y() - center_label.y()
                dist = (dx**2 + dy**2)**0.5
                radius = self.magnifier_radius
                tol = 10

                # Check if clicking the border or inside
                if abs(dist - radius) <= tol:
                    self.parent_viewer.resizing_magnifier = True
                    self.parent_viewer.start_mouse_label = mouse_label
                    self.parent_viewer.start_radius = radius
                    return
                elif dist < radius - tol:
                    self.parent_viewer.dragging_magnifier = True
                    self.parent_viewer.drag_offset = mouse_label - center_label
                    return
        elif event.button() == Qt.MiddleButton:
            self.parent_viewer.pan_start_pos = event.globalPos()
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        scroll_x = self.parent_viewer.scroll_area.horizontalScrollBar().value()
        scroll_y = self.parent_viewer.scroll_area.verticalScrollBar().value()
        mouse_label = event.pos() + QPoint(scroll_x, scroll_y)
        image_x = (event.pos().x() + scroll_x) / self.parent_viewer.zoom
        image_y = (event.pos().y() + scroll_y) / self.parent_viewer.zoom
        position_text = f"X: {int(image_x)}, Y: {int(image_y)}"
        if self.parent_viewer.geo_info is not None:
            center_lat, center_lon, width, height, pixel_size = self.parent_viewer.geo_info
            meters_per_deg_lat, meters_per_deg_lon = meters_per_degree(center_lat)

            center_x = width // 2
            center_y = height // 2

            dx_m = (image_x - center_x) * pixel_size    # east-west offset in meters
            dy_m = (center_y - image_y) * pixel_size    # north-south offset in meters

            lat = center_lat + (dy_m / meters_per_deg_lat)
            lon = center_lon + (dx_m / meters_per_deg_lon)
            position_text += f" | <b>Lat: {lat:.8f}, Lon: {lon:.8f}</b>"

        position_text += f" | Zoom: {self.zoom * 100:.1f}% | Rotation: {self.parent_viewer.rotation:.1f}°"
        self.parent_viewer.position_label.setText(position_text)

        # Update cursor based on mouse position
        if self.magnifier_enabled:
            center_label = QPoint(
                int(self.magnifier_center.x() * self.parent_viewer.zoom),
                int(self.magnifier_center.y() * self.parent_viewer.zoom)
            )
            dx = mouse_label.x() - center_label.x()
            dy = mouse_label.y() - center_label.y()
            dist = (dx**2 + dy**2)**0.5
            radius = self.magnifier_radius
            tol = 10

            if abs(dist - radius) <= tol:
                self.setCursor(Qt.SizeFDiagCursor)
            elif dist < radius - tol:
                self.setCursor(Qt.SizeAllCursor)
            else:
                self.unsetCursor()
        else:
            self.unsetCursor()

        # Handle dragging or resizing
        if self.magnifier_enabled and self.parent_viewer.dragging_magnifier:
            new_center_label = mouse_label - self.parent_viewer.drag_offset
            new_center_img = QPointF(
                new_center_label.x() / self.parent_viewer.zoom,
                new_center_label.y() / self.parent_viewer.zoom
            )
            new_center_img.setX(max(0, min(self.parent_viewer.full_width - 1, new_center_img.x())))
            new_center_img.setY(max(0, min(self.parent_viewer.full_height - 1, new_center_img.y())))
            self.magnifier_center = new_center_img.toPoint()
            self.update()
        elif self.magnifier_enabled and self.parent_viewer.resizing_magnifier:
            center_label = QPoint(
                int(self.magnifier_center.x() * self.parent_viewer.zoom),
                int(self.magnifier_center.y() * self.parent_viewer.zoom)
            )
            dx = mouse_label.x() - center_label.x()
            dy = mouse_label.y() - center_label.y()
            current_dist = (dx**2 + dy**2)**0.5
            start_dx = self.parent_viewer.start_mouse_label.x() - center_label.x()
            start_dy = self.parent_viewer.start_mouse_label.y() - center_label.y()
            start_dist = (start_dx**2 + start_dy**2)**0.5
            new_radius = self.parent_viewer.start_radius + (current_dist - start_dist)
            new_radius = max(20, min(400, int(new_radius)))
            self.magnifier_radius = new_radius
            self.update()

        # Pan handling
        if event.buttons() & Qt.MiddleButton and self.parent_viewer.pan_start_pos:
            current_pos = event.globalPos()
            delta = current_pos - self.parent_viewer.pan_start_pos
            self.parent_viewer.pan_start_pos = current_pos
            h_scroll = self.parent_viewer.scroll_area.horizontalScrollBar()
            v_scroll = self.parent_viewer.scroll_area.verticalScrollBar()
            h_scroll.setValue(h_scroll.value() - delta.x())
            v_scroll.setValue(v_scroll.value() - delta.y())

        # Pixel info callback with matrix and lat/lon scaling
        if self.parent_viewer.pixel_info_callback:
            matrix_size = self.parent_viewer.matrix_size_var.value()
            half = matrix_size // 2
            x0 = max(0, int(image_x - half))
            x1 = min(self.parent_viewer.full_width, int(image_x + half + 1))
            y0 = max(0, int(image_y - half))
            y1 = min(self.parent_viewer.full_height, int(image_y + half + 1))
            sub_array = self.parent_viewer.original_image_data[y0:y1, x0:x1] if self.parent_viewer.original_image_data is not None else np.zeros((matrix_size, matrix_size), dtype=np.uint8)
            if sub_array.ndim == 3:
                self.parent_viewer.pixel_info_callback(image_x, image_y, sub_array, is_rgb=True, full_width=self.parent_viewer.full_width, full_height=self.parent_viewer.full_height)
            else:
                self.parent_viewer.pixel_info_callback(image_x, image_y, sub_array, is_rgb=False, full_width=self.parent_viewer.full_width, full_height=self.parent_viewer.full_height)

        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton:
            self.parent_viewer.dragging_magnifier = False
            self.parent_viewer.resizing_magnifier = False
        if event.button() == Qt.MiddleButton:
            self.parent_viewer.pan_start_pos = None
        super().mouseReleaseEvent(event)

    def wheelEvent(self, event):
        if event.modifiers() & Qt.ControlModifier:
            delta = event.angleDelta().y()
            mouse_pos = event.pos()
            old_zoom = self.parent_viewer.zoom
            if delta > 0:
                zoom_factor = 1.25
            else:
                zoom_factor = 0.8
            new_zoom = old_zoom * zoom_factor
            scroll_x = self.parent_viewer.scroll_area.horizontalScrollBar().value()
            scroll_y = self.parent_viewer.scroll_area.verticalScrollBar().value()
            mouse_image_x = (mouse_pos.x() + scroll_x) / old_zoom
            mouse_image_y = (mouse_pos.y() + scroll_y) / old_zoom
            new_scroll_x = mouse_image_x * new_zoom - mouse_pos.x()
            new_scroll_y = mouse_image_y * new_zoom - mouse_pos.y()
            self.parent_viewer.zoom = new_zoom
            self.parent_viewer.show_image(self.parent_viewer.current_pil_image, fit_to_screen=False)
            self.parent_viewer.scroll_area.horizontalScrollBar().setValue(int(new_scroll_x))
            self.parent_viewer.scroll_area.verticalScrollBar().setValue(int(new_scroll_y))

            if self.magnifier_enabled:
                # Use updated scroll values
                updated_scroll_x = self.parent_viewer.scroll_area.horizontalScrollBar().value()
                updated_scroll_y = self.parent_viewer.scroll_area.verticalScrollBar().value()
                image_x = (mouse_pos.x() + updated_scroll_x) / self.parent_viewer.zoom
                image_y = (mouse_pos.y() + updated_scroll_y) / self.parent_viewer.zoom
                self.magnifier_center = QPointF(image_x, image_y)
                self.update()  # Force repaint

            event.accept()
        else:
            super().wheelEvent(event)


class ZoomableImageViewer(QWidget):
    def __init__(self, parent=None, image=None, pixel_info_callback=None, matrix_size_var=None, lat_lon_data=None):
        super().__init__(parent)
        self.parent_viewer = parent  # Assuming parent_viewer is the parent widget
        self.geo_info = None
        self.image = None
        self.original_image_data = None
        self.zoom = 1.0
        self.rotation = 0.0
        self.pixel_info_callback = pixel_info_callback
        self.matrix_size_var = matrix_size_var
        self.lat_lon_data = lat_lon_data
        self.selection_pos = None
        self.full_width = 0
        self.full_height = 0
        self.displayed_image = None
        self.pan_start_pos = None
        self.dragging_magnifier = False
        self.resizing_magnifier = False
        self.drag_offset = None
        self.start_mouse_label = None
        self.start_radius = 0
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.StrongFocus)
        
        layout = QVBoxLayout()
        self.setLayout(layout)
        
        self.scroll_area = QScrollArea()
        self.scroll_area.setWidgetResizable(True)
        self.scroll_area.setMouseTracking(True)  # Enable smooth tracking over scroll area
        self.fs_btn = QPushButton("⛶", self.scroll_area.viewport())
        self.fs_btn.setFixedSize(40, 40)
        self.fs_btn.setStyleSheet("background: rgba(0,0,0,70); color: white; border-radius: 5px;")
        self.fs_btn.clicked.connect(lambda: self.image_label.toggle_fullscreen())
        # ensure button is visible and raised above the viewport contents
        self.fs_btn.show()
        self.fs_btn.raise_()

        # reposition function + eventFilter so the button stays in the bottom-right corner of the viewport
        self.scroll_area.viewport().installEventFilter(self)
        self._reposition_fs_button()
        self.image_label = ImageLabel(self)
        self.image_label.lat_lon_data = self.lat_lon_data
        self.image_label.setAlignment(Qt.AlignCenter)
        self.image_label.setMouseTracking(True)  # Enable smooth tracking over image label
        self.scroll_area.setWidget(self.image_label)
        layout.addWidget(self.scroll_area)
        
        self.position_label = QLabel()
        self.position_label.setAlignment(Qt.AlignLeft)
        layout.addWidget(self.position_label)
        
        magnifier_layout = QHBoxLayout()
        self.magnifier_toggle = QCheckBox("Enable Magnifier")
        self.magnifier_toggle.stateChanged.connect(self.toggle_magnifier)
        magnifier_layout.addWidget(self.magnifier_toggle)
        
        self.magnifier_zoom_slider = QSlider(Qt.Horizontal)
        self.magnifier_zoom_slider.setRange(1, 20)
        self.magnifier_zoom_slider.setValue(8)
        self.magnifier_zoom_slider.valueChanged.connect(self.update_magnifier_zoom)
        magnifier_layout.addWidget(self.magnifier_zoom_slider)
        layout.addLayout(magnifier_layout)

        self.scroll_area.setWidgetResizable(False)
        self.scroll_area.setAlignment(Qt.AlignCenter)
        
        if image:
            self.show_image(image)

    def eventFilter(self, obj, event):
        # keep fs_btn anchored to bottom-right of the viewport
        if obj == self.scroll_area.viewport() and event.type() in (QEvent.Resize, QEvent.Paint):
            try:
                self._reposition_fs_button()
            except Exception:
                pass
        return super().eventFilter(obj, event)

    def _reposition_fs_button(self):
        try:
            vp = self.scroll_area.viewport()
            btn = getattr(self, 'fs_btn', None)
            if not btn:
                return
            margin = 10
            x = max(0, vp.width() - btn.width() - margin)
            y = max(0, vp.height() - btn.height() - margin)
            btn.move(x, y)
            btn.raise_()
            btn.show()
        except Exception:
            pass


    def toggle_magnifier(self, state):
        if state == Qt.Checked:
            if self.original_image_data is None:
                self.magnifier_toggle.setChecked(False)
                return
            self.image_label.magnifier_enabled = True
            if self.image_label.magnifier_center is None:
                scroll_x = self.scroll_area.horizontalScrollBar().value()
                scroll_y = self.scroll_area.verticalScrollBar().value()
                vp_w = self.scroll_area.viewport().width()
                vp_h = self.scroll_area.viewport().height()
                pixmap = self.image_label.pixmap()
                if pixmap is None:
                    return
                pixmap_w = pixmap.width()
                pixmap_h = pixmap.height()
                offset_x = max(0, (vp_w - pixmap_w) / 2)
                offset_y = max(0, (vp_h - pixmap_h) / 2)
                center_label_x = scroll_x + vp_w / 2 - offset_x
                center_label_y = scroll_y + vp_h / 2 - offset_y
                center_img_x = center_label_x / self.zoom
                center_img_y = center_label_y / self.zoom
                self.image_label.magnifier_center = QPoint(int(center_img_x), int(center_img_y))
        else:
            self.image_label.magnifier_enabled = False
        self.image_label.update()

    def update_magnifier_zoom(self, value):
        self.image_label.magnifier_zoom = value
        self.image_label.update()

    def show_image(self, image, fit_to_screen=False):
        print(f"Showing image: {image is not None}, fit_to_screen: {fit_to_screen}")
        self.image = image
        if not image:
            self.image_label.clear()
            self.full_width = 0
            self.full_height = 0
            self.original_image_data = None
            return
            
        self.full_width, self.full_height = image.size
        if image.mode == 'RGB':
            self.original_image_data = np.array(image.convert('RGB'))
            print(f"Set original_image_data: RGB, shape: {self.original_image_data.shape}")
        else:
            self.original_image_data = np.array(image.convert('L'))
            print(f"Set original_image_data: Grayscale, shape: {self.original_image_data.shape}")
            
        if fit_to_screen:
            self.fit_to_screen()
        else:
            self.zoom = 1.0
            self.rotation = 0.0
            self._update_image()
    
    def fit_to_screen(self):
        if not self.image:
            return
            
        viewport_size = self.scroll_area.viewport().size()
        img_width, img_height = self.full_width, self.full_height
        
        zoom_w = viewport_size.width() / img_width
        zoom_h = viewport_size.height() / img_height
        self.zoom = min(zoom_w, zoom_h) * 0.95
        self.rotation = 0.0
        self._update_image()
        
        self.scroll_area.horizontalScrollBar().setValue(0)
        self.scroll_area.verticalScrollBar().setValue(0)
    
    def zoom_image(self, factor):
        new_zoom = self.zoom * factor
        new_zoom = max(0.1, min(5.0, new_zoom))
        if new_zoom != self.zoom:
            self.zoom = new_zoom
            self._update_image()
    
    def _update_image(self):
        if not self.image:
            print("No image to update in ZoomableImageViewer")
            return

        try:
            width = int(self.full_width * self.zoom)
            height = int(self.full_height * self.zoom)
            
            rotated = self.image.rotate(self.rotation, expand=True)
            scaled = rotated.resize((width, height), Image.LANCZOS)
            self.displayed_image = scaled

            img_array = np.array(scaled)
            if scaled.mode == 'RGB':
                if len(img_array.shape) != 3 or img_array.shape[2] != 3:
                    print("Invalid RGB image shape")
                    return
                img_array = np.ascontiguousarray(img_array)
                qimage = QImage(img_array.data, width, height, width * 3, QImage.Format_RGB888)
            else:
                img_array = np.ascontiguousarray(img_array)
                qimage = QImage(img_array.data, width, height, width, QImage.Format_Grayscale8)
            
            if qimage.isNull():
                print("Failed to create QImage")
                return
            
            pixmap = QPixmap.fromImage(qimage)
            self.image_label.setPixmap(pixmap)
            self.image_label.resize(pixmap.size())
            del img_array, qimage, pixmap
            gc.collect()
        except Exception as e:
            print(f"Error updating image: {e}")
    
    def wheelEvent(self, event):
        pos = event.pos()
        scroll_bar = self.scroll_area.horizontalScrollBar()
        x = scroll_bar.value() + pos.x()
        y = self.scroll_area.verticalScrollBar().value() + pos.y()
        
        factor = 1.1 if event.angleDelta().y() > 0 else 0.9
        new_zoom = self.zoom * factor
        new_zoom = max(0.1, min(5.0, new_zoom))
        
        if new_zoom != self.zoom:
            rel_x = x / (self.full_width * self.zoom)
            rel_y = y / (self.full_height * self.zoom)
            self.zoom = new_zoom
            new_x = rel_x * (self.full_width * self.zoom) - pos.x()
            new_y = rel_y * (self.full_height * self.zoom) - pos.y()
            self._update_image()
            self.scroll_area.horizontalScrollBar().setValue(int(new_x))
            self.scroll_area.verticalScrollBar().setValue(int(new_y))
    
    
    def _emit_pixel_info_at(self, x, y):
        if not self.image or not self.pixel_info_callback or self.original_image_data is None:
            print("Cannot show pixel info: missing image, callback, or original_image_data")
            return

        if not (0 <= x < self.full_width and 0 <= y < self.full_height):
            print(f"Click out of bounds: ({x}, {y})")
            self.pixel_info_callback(x, y, np.zeros((3, 3), dtype=np.uint8), is_rgb=False)
            return

        matrix_size = self.matrix_size_var.value() // 2 if self.matrix_size_var else 1
        target_size = matrix_size * 2 + 1
        start_x = max(0, x - matrix_size)
        end_x = min(self.full_width, x + matrix_size + 1)
        start_y = max(0, y - matrix_size)
        end_y = min(self.full_height, y + matrix_size + 1)

        # Always use original_image_data, not contrast-enhanced data
        matrix = self.original_image_data[start_y:end_y, start_x:end_x]

        if self.original_image_data.ndim == 3:
            padded_matrix = np.zeros((target_size, target_size, 3), dtype=np.uint8)
            h, w = matrix.shape[:2]
            padded_matrix[:h, :w] = matrix
            is_rgb = True
        else:
            padded_matrix = np.zeros((target_size, target_size), dtype=np.uint8)
            h, w = matrix.shape
            padded_matrix[:h, :w] = matrix
            is_rgb = False

        self.pixel_info_callback(x, y, padded_matrix, is_rgb=is_rgb)

    def show_pixel_info(self, event):
        if not self.image or not self.pixel_info_callback or self.original_image_data is None:
            print("Cannot show pixel info: missing image, callback, or original_image_data")
            return
            
        scroll_x = self.scroll_area.horizontalScrollBar().value()
        scroll_y = self.scroll_area.verticalScrollBar().value()
        x = int((event.x() + scroll_x) / self.zoom)
        y = int((event.y() + scroll_y) / self.zoom)
        
        if not (0 <= x < self.full_width and 0 <= y < self.full_height):
            print(f"Click out of bounds: ({x}, {y})")
            self.pixel_info_callback(x, y, np.zeros((3, 3), dtype=np.uint8), is_rgb=False)
            return
            
        self.selection_pos = (x, y)
        self.image_label.update()
        self._emit_pixel_info_at(x, y)

class HistogramViewer(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.figure = Figure(figsize=(6, 4), dpi=100)
        self.ax = self.figure.add_subplot(111)
        self.canvas = FigureCanvas(self.figure)
        
        layout = QVBoxLayout()
        self.setLayout(layout)
        layout.addWidget(self.canvas)
        
        control_layout = QHBoxLayout()
        layout.addLayout(control_layout)
        
        fit_btn = QPushButton("Fit")
        fit_btn.setEnabled(False)
        control_layout.addWidget(fit_btn)
        
        self.single_frame_radio = QRadioButton("Single Frame")
        self.single_frame_radio.setChecked(True)
        control_layout.addWidget(self.single_frame_radio)
        
        self.frame_range_radio = QRadioButton("Frame Range")
        control_layout.addWidget(self.frame_range_radio)
        
        self.min_val = 255
        self.max_val = 0
    
    def update_histogram(self, band_frames, current_frame_index, frame_mode, start_frame=None, end_frame=None, smooth=True, ignore_extremes=True):
        """
        band_frames: dict like {'b0_left': LazyFrames(...), ...}
        current_frame_index: int (0-based)
        frame_mode: "Single" or "Range"
        ignore_extremes: if True excludes pixels ==0 or ==255 from the histogram (removes background spikes)
        """
        self.ax.clear()
        if not band_frames:
            self.canvas.draw()
            return

        colors = ['red', 'green', 'blue', 'cyan', 'magenta', 'yellow', 'black']
        self.max_val = 0
        self.min_val = 255

        # prepare bin centers 0..255
        bins = np.arange(0, 257, dtype=np.int32)
        bin_centers = (bins[:-1] + bins[1:]) / 2.0

        for i, (key, frames) in enumerate(band_frames.items()):
            # gather pixel values depending on single vs range
            if frame_mode == "Single":
                if current_frame_index < len(frames):
                    img = np.asarray(frames[current_frame_index])
                    img_data = img.ravel().astype(np.int32)
                else:
                    continue
            else:  # Range
                collected = []
                for idx in range(start_frame, end_frame + 1):
                    if idx < len(frames):
                        collected.append(np.asarray(frames[idx]).ravel().astype(np.int32))
                if not collected:
                    continue
                img_data = np.concatenate(collected)

            # clip safe and update min/max
            img_data = np.clip(img_data, 0, 255)
            if img_data.size == 0:
                continue
            mean = float(np.mean(img_data))
            sd = float(np.std(img_data))
            self.max_val = max(self.max_val, int(np.max(img_data)))
            self.min_val = min(self.min_val, int(np.min(img_data)))

            # optional: ignore saturated/background pixels (0 and 255)
            if ignore_extremes:
                mask = (img_data > 0) & (img_data < 255)
                filtered = img_data[mask]
                # if too few pixels remain, fall back to full data
                if filtered.size > max(100, img_data.size * 0.01):  # keep if >100 pixels or >1% of total
                    used = filtered
                else:
                    # fallback: keep full array but print note for debugging
                    print(f"HistogramViewer: band {key} has too few non-saturated pixels (kept full data).")
                    used = img_data
            else:
                used = img_data

            # compute exact integer histogram with bincount
            counts = np.bincount(used.astype(np.int32), minlength=256).astype(np.float64)
            total = counts.sum()
            if total > 0:
                hist = counts / total
            else:
                hist = counts

            # optional smoothing (triangular kernel)
            if smooth:
                kernel = np.array([1.0, 2.0, 1.0], dtype=np.float64)
                kernel /= kernel.sum()
                hist = np.convolve(hist, kernel, mode='same')

            label = f"Band {key[1:]} [{'Frame ' + str(current_frame_index+1) if frame_mode=='Single' else f'Frames {start_frame+1}-{end_frame+1}'}] | mean={mean:.2f}, sd={sd:.2f}"
            self.ax.plot(bin_centers, hist, color=colors[i % len(colors)], alpha=0.85, label=label)

        # set x limits (leave a bit padding so extremes are visible if present)
        if self.max_val > self.min_val:
            left = max(0, self.min_val - 1)
            right = min(255, self.max_val + 1)
            self.ax.set_xlim(left, right)
        else:
            self.ax.set_xlim(0, 255)

        self.ax.set_xlabel('Pixel Value')
        self.ax.set_ylabel('Normalized Frequency')
        self.ax.set_title(
            f"Normalized Pixel Intensity Distribution (Frame {current_frame_index+1})" if frame_mode == "Single"
            else f"Normalized Pixel Intensity Distribution (Frames {start_frame+1} to {end_frame+1})"
        )
        self.ax.legend(loc='upper right', fontsize='small')
        self.canvas.draw()



class CustomTabBar(QTabBar):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTabsClosable(True)
        self.setMovable(True)
        self.setDrawBase(False)

class TerminalWidget(QWidget):
    """
    Inline terminal widget using a single QTextEdit for output and input.
    Commands are typed directly after the prompt. Supports history (Up/Down), Ctrl+L to clear.
    'cd' built-in handled in-Python so cwd/prompt updates.
    Commands executed with QProcess using widget cwd.
    """
    output_signal = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        

        # Prompt identity
        self.user = getpass.getuser()
        self.host = socket.gethostname().split('.')[0]
        self.cwd = os.getcwd()

        # Layout
        root = QVBoxLayout(self)
        root.setContentsMargins(4, 4, 4, 4)
        root.setSpacing(4)

        # Single QTextEdit for output and input
        self.terminal = QTextEdit()
        self.terminal.setReadOnly(False)  # Allow editing
        self.terminal.setAcceptRichText(False)
        try:
            f = self.terminal.font()
            f.setFamily("Courier New")
            f.setPointSize(10)
            self.terminal.setFont(f)
        except Exception:
            pass
        root.addWidget(self.terminal, 1)

        # Install event filter for key handling
        self.terminal.installEventFilter(self)

        # History
        self._history = []
        self._hist_idx = None
        self._history_limit = 500
        self._current_input = ""  # Buffer for current line input

        # Default shell
        sys_platform = platform.system().lower()
        if 'windows' in sys_platform:
            self._default_shell = shutil.which("powershell.exe") or shutil.which("cmd.exe") or "cmd.exe"
            self._use_cmd = True
        else:
            self._default_shell = shutil.which("bash") or shutil.which("sh") or "/bin/sh"
            self._use_cmd = False

        # Start closed
        self.setMaximumHeight(0)
        self.hide()

        self.output_signal.connect(self._append_text)
        self._append_text(f"[Terminal started: shell={self._default_shell} cwd={self.cwd}]\n")
        self._insert_prompt()

    def _insert_prompt(self):
        prompt = self._build_prompt() + " "
        self.terminal.moveCursor(QTextCursor.End)
        self.terminal.insertPlainText(prompt)
        self.terminal.moveCursor(QTextCursor.End)

    def _build_prompt(self):
        home = os.path.expanduser("~")
        if self.cwd == home:
            cwd_display = "~"
        elif self.cwd.startswith(home + os.sep):
            cwd_display = "~" + self.cwd[len(home):]
        else:
            cwd_display = self.cwd
        return f"{self.user}@{self.host}:{cwd_display}$"

    def _append_text(self, txt):
        self.terminal.moveCursor(QTextCursor.End)
        self.terminal.insertPlainText(txt)
        self.terminal.moveCursor(QTextCursor.End)

    def eventFilter(self, obj, event):
        if obj == self.terminal and event.type() == QEvent.KeyPress:
            key = event.key()
            mod = event.modifiers()

            if key == Qt.Key_Return or key == Qt.Key_Enter:
                # Get the current line after prompt
                cursor = self.terminal.textCursor()
                cursor.movePosition(QTextCursor.End)
                cursor.select(QTextCursor.LineUnderCursor)
                line = cursor.selectedText()
                prompt_len = len(self._build_prompt() + " ")
                cmd = line[prompt_len:]  # No strip() to preserve trailing spaces if needed

                # Add newline after command
                self._append_text("\n")

                # Update history
                cmd_stripped = cmd.strip()
                if cmd_stripped:
                    if not (self._history and self._history[-1] == cmd_stripped):
                        self._history.append(cmd_stripped)
                        if len(self._history) > self._history_limit:
                            self._history.pop(0)
                self._hist_idx = None
                self._current_input = ""

                # Builtins
                
                parts = shlex.split(cmd)
                if parts and parts[0] == "cd":
                    if len(parts) == 1 or parts[1] == "~":
                        target = os.path.expanduser("~")
                    else:
                        target = os.path.expanduser(parts[1])
                        if not os.path.isabs(target):
                            target = os.path.normpath(os.path.join(self.cwd, target))
                    try:
                        os.chdir(target)
                        self.cwd = os.getcwd()
                    except Exception as e:
                        self._append_text(f"cd: {e}\n")
                    self._insert_prompt()
                    return True

                if cmd_stripped in ("clear", "cls"):
                    self.terminal.clear()
                    self._insert_prompt()
                    return True
                        
                # Run external command (async, prompt after finish)
                self._run_command(cmd)
                return True

            elif key == Qt.Key_Up:
                if self._hist_idx is None:
                    self._hist_idx = len(self._history)
                if self._hist_idx > 0:
                    self._hist_idx -= 1
                    self._replace_current_input(self._history[self._hist_idx])
                return True

            elif key == Qt.Key_Down:
                if self._hist_idx is not None and self._hist_idx < len(self._history) - 1:
                    self._hist_idx += 1
                    self._replace_current_input(self._history[self._hist_idx])
                elif self._hist_idx is not None:
                    self._hist_idx = None
                    self._replace_current_input("")
                return True
            
            elif key == Qt.Key_Tab:
                    self._do_completion()
                    return True
            
            elif key == Qt.Key_L and mod == Qt.ControlModifier:
                self.terminal.clear()
                self._insert_prompt()
                return True

            elif key == Qt.Key_Backspace:
                # Prevent deleting before prompt
                cursor = self.terminal.textCursor()
                cursor.movePosition(QTextCursor.End)
                cursor.select(QTextCursor.LineUnderCursor)
                line = cursor.selectedText()
                prompt_len = len(self._build_prompt() + " ")
                if len(line) <= prompt_len:
                    return True  # Block backspace

            # Allow other keys to type normally
            return False

        return super().eventFilter(obj, event)

    def _do_completion(self):
        cursor = self.terminal.textCursor()
        cursor.movePosition(QTextCursor.End)
        cursor.select(QTextCursor.LineUnderCursor)
        line = cursor.selectedText()
        prompt_len = len(self._build_prompt() + " ")
        text = line[prompt_len:]

        if not text.strip():
            return

        parts = text.split()
        prefix = parts[-1]

        # If it's the first word, complete commands
        if len(parts) == 1:
            paths = []
            for p in os.environ.get("PATH", "").split(os.pathsep):
                if os.path.isdir(p):
                    paths.extend(glob.glob(os.path.join(p, prefix) + "*"))
            matches = [os.path.basename(m) for m in paths if os.access(m, os.X_OK)]
        else:
            # File/directory completion
            matches = glob.glob(prefix + "*")

        if len(matches) == 1:
            parts[-1] = matches[0]
            new_line = " ".join(parts)
            self._replace_current_input(new_line)
        elif len(matches) > 1:
            self._append_text("\n" + "  ".join(matches) + "\n")
            self._insert_prompt()
            self._replace_current_input(text)

    def _replace_current_input(self, text):
        cursor = self.terminal.textCursor()
        cursor.movePosition(QTextCursor.End)
        cursor.select(QTextCursor.LineUnderCursor)
        line = cursor.selectedText()
        prompt = line[:len(self._build_prompt() + " ")]
        cursor.removeSelectedText()
        cursor.insertText(prompt + text)
        cursor.movePosition(QTextCursor.End)
        self.terminal.setTextCursor(cursor)
        self._current_input = text

    def _run_command(self, cmd):
        proc = QProcess(self)
        proc.setProcessChannelMode(QProcess.MergedChannels)
        proc.setWorkingDirectory(self.cwd)

        def on_ready_read():
            try:
                data = proc.readAll().data().decode(errors="ignore")
            except Exception:
                data = ""
            if data:
                self.output_signal.emit(data)

        def on_finished(exit_code, exit_status):
            # Read any remaining data that might not have triggered readyRead
            try:
                remaining_data = proc.readAll().data().decode(errors="ignore")
            except Exception:
                remaining_data = ""
            if remaining_data:
                self.output_signal.emit(remaining_data)
            
            try:
                proc.readyRead.disconnect(on_ready_read)
            except Exception:
                pass
            try:
                proc.finished.disconnect(on_finished)
            except Exception:
                pass
            proc.deleteLater()
            self._insert_prompt()

        proc.readyRead.connect(on_ready_read)
        proc.finished.connect(on_finished)

        if isinstance(self._default_shell, str) and 'cmd.exe' in self._default_shell.lower():
            proc.start(self._default_shell, ["/C", cmd])
        else:
            proc.start(self._default_shell, ["-c", cmd])

    def focus_input(self):
        self.terminal.setFocus()
        self.terminal.moveCursor(QTextCursor.End)

class BandStitchProApp(QWidget):
    def __init__(self, parent=None, main_app=None):
        super().__init__(parent)
        self.main_app = main_app
        self.band_frames = {}
        self.bitdepth = 10
        self.base_name = ""
        self.current_frame_index = 0
        self.playing = False
        self.play_delay = 100
        self.band_offsets = {f"b{i}": {"x": 0, "y": 0} for i in range(7)}
        self.band_enabled = {}
        self.band_gaps = 10
        self.rgb_bands = {"R": "b0", "G": "b1", "B": "b2"}
        self.current_folder = None
        self.lat_lon_data = None
        
        self.matrix_size_var = QSpinBox()
        self.matrix_size_var.setRange(3, 9)
        self.matrix_size_var.setSingleStep(2)
        self.matrix_size_var.setValue(3)
        
        self.width_entry = QLineEdit("8448")
        self.height_entry = QLineEdit("384")
        self.bitdepth_var = QComboBox()
        self.bitdepth_var.addItems(["8", "10", "12"])
        self.bitdepth_var.setCurrentIndex(1)
        
        self.gap_var = QSpinBox()
        self.gap_var.setRange(0, 50)
        self.gap_var.setValue(10)
        
        self.contrast_enhance_var = QCheckBox("Contrast Enhance")
        self.contrast_enhance_var.setChecked(False)  
        self.contrast_min_var = QDoubleSpinBox()
        self.contrast_min_var.setRange(0, 255)
        self.contrast_min_var.setValue(0)
        self.contrast_max_var = QDoubleSpinBox()
        self.contrast_max_var.setRange(0, 255)
        self.contrast_max_var.setValue(255)
        self.contrast_enhance_var.stateChanged.connect(self._invalidate_cache)

        self.frame_mode_var = QButtonGroup()
        self.frame_mode_single = QRadioButton("Single Frame")
        self.frame_mode_single.setChecked(True)
        self.frame_mode_range = QRadioButton("Frame Range")
        self.frame_mode_var.addButton(self.frame_mode_single, 0)
        self.frame_mode_var.addButton(self.frame_mode_range, 1)
        
        self.rgb_frame_mode_var = QButtonGroup()
        self.rgb_frame_mode_single = QRadioButton("Selected Frame")
        self.rgb_frame_mode_single.setChecked(True)
        self.rgb_frame_mode_all = QRadioButton("All Frames")
        self.rgb_frame_mode_var.addButton(self.rgb_frame_mode_single, 0)
        self.rgb_frame_mode_var.addButton(self.rgb_frame_mode_all, 1)
        
        self.fit_mode_var = QButtonGroup()
        self.fit_mode_screen = QRadioButton("Fit to Screen")
        self.fit_mode_actual = QRadioButton("Actual Size")
        self.fit_mode_actual.setChecked(True)
        self.fit_mode_var.addButton(self.fit_mode_screen, 0)
        self.fit_mode_var.addButton(self.fit_mode_actual, 1)
        
        self.start_frame_entry = QSpinBox()
        self.start_frame_entry.setRange(1, 1000)
        self.start_frame_entry.setValue(1)
        self.end_frame_entry = QSpinBox()
        self.end_frame_entry.setRange(1, 1000)
        self.end_frame_entry.setValue(1)
        self.view_cache = {}  # {tab_name: {'frame_index': int, 'pil_image': Image, 'original_data': np.array, 'hash': str}}
        self.param_hash = self._compute_param_hash()  # Initial hash of offsets, contrast, etc.
        

        # Keyboard shortcuts
        QShortcut(QKeySequence("Shift+N"), self, self.main_app.add_new_tab)
        QShortcut(QKeySequence("Shift+Q"), self, lambda: self.main_app.close_tab(self.main_app.tab_widget.indexOf(self)))
        QShortcut(QKeySequence("Shift+Return"), self, self.select_folder)
        QShortcut(QKeySequence("Ctrl+S"), self, self.save_parameters)
        QShortcut(QKeySequence("Tab"), self, self.cycle_view_tabs_forward)
        QShortcut(QKeySequence("Shift+Tab"), self, self.cycle_view_tabs_backward)
        QShortcut(QKeySequence("Right"), self, lambda: self.change_frame(1))
        QShortcut(QKeySequence("Left"), self, lambda: self.change_frame(-1))
        QShortcut(QKeySequence("Ctrl+Up"), self, self.zoom_in)
        QShortcut(QKeySequence("Ctrl+Down"), self, self.zoom_out)
        QShortcut(QKeySequence("Space"), self, self.toggle_play)
        QShortcut(QKeySequence("Return"), self, self.update_views)
        QShortcut(QKeySequence("Ctrl+Return"), self, self.apply_contrast_enhancement)
        QShortcut(QKeySequence("Ctrl+Space"), self, self.export_current_image)
        QShortcut(QKeySequence("F11"), self, self.toggle_fullscreen)

        self.init_ui()
    
    def _compute_param_hash(self):
        data = json.dumps({
            'offsets': self.band_offsets,
            'contrast_enhance': self.contrast_enhance_var.isChecked(),
            'contrast_min': self.contrast_min_var.value(),
            'contrast_max': self.contrast_max_var.value(),
            'gap': self.gap_var.value()
        }, sort_keys=True)
        return hashlib.md5(data.encode()).hexdigest()

    def init_ui(self):
        main_layout = QHBoxLayout()
        main_layout.setContentsMargins(5, 5, 5, 5)
        self.setLayout(main_layout)
        
        self.left_scroll = QScrollArea()
        self.left_scroll.setWidgetResizable(True)
        self.left_panel = QWidget()
        left_layout = QVBoxLayout()
        self.left_panel.setLayout(left_layout)
        self.left_scroll.setWidget(self.left_panel)
        self.left_scroll.setFixedWidth(500)
        main_layout.addWidget(self.left_scroll)
        
        self.folder_label = QLabel("No folder selected")
        self.folder_label.setWordWrap(True)
        left_layout.addWidget(self.folder_label)
        
        select_btn = QPushButton("Select Folder & Stitch")
        select_btn.clicked.connect(self.select_folder)
        left_layout.addWidget(select_btn)
        
        frame_group = QGroupBox("Frame Controls")
        frame_layout = QVBoxLayout()
        frame_group.setLayout(frame_layout)
        left_layout.addWidget(frame_group)
        
        self.frame_slider = QSlider(Qt.Horizontal)
        self.frame_slider.setRange(0, 0)
        frame_layout.addWidget(self.frame_slider)
        
        self.frame_label = QLabel("0/0")
        frame_layout.addWidget(self.frame_label)
        
        playback_layout = QHBoxLayout()
        self.play_btn = QPushButton("▶ Play")
        self.play_btn.clicked.connect(self.toggle_play)
        playback_layout.addWidget(self.play_btn)
        
        prev_btn = QPushButton("◀")
        prev_btn.clicked.connect(lambda: self.change_frame(-1))
        playback_layout.addWidget(prev_btn)
        
        next_btn = QPushButton("▶")
        next_btn.clicked.connect(lambda: self.change_frame(1))
        playback_layout.addWidget(next_btn)
        
        frame_layout.addLayout(playback_layout)
        
        frame_mode_layout = QHBoxLayout()
        frame_mode_layout.addWidget(self.frame_mode_single)
        frame_mode_layout.addWidget(self.frame_mode_range)
        frame_layout.addLayout(frame_mode_layout)
        
        frame_range_layout = QHBoxLayout()
        frame_range_layout.addWidget(QLabel("Start:"))
        frame_range_layout.addWidget(self.start_frame_entry)
        frame_range_layout.addWidget(QLabel("End:"))
        frame_range_layout.addWidget(self.end_frame_entry)
        frame_layout.addLayout(frame_range_layout)
        
        offset_frame = QGroupBox("Band Offsets")
        offset_layout = QVBoxLayout()
        offset_frame.setLayout(offset_layout)
        offset_frame.setCheckable(True)
        offset_frame.setChecked(True)
        left_layout.addWidget(offset_frame)
        
        self.offset_spins = {}
        for i in range(7):
            row_layout = QHBoxLayout()
            row_layout.addWidget(QLabel(f"Band {i}:"))
            spin_x = QSpinBox()
            spin_x.setRange(-100, 100)
            spin_x.valueChanged.connect(lambda v, idx=i: self.update_offset_value(idx, 'x', v))
            row_layout.addWidget(QLabel("x:"))
            row_layout.addWidget(spin_x)
            spin_y = QSpinBox()
            spin_y.setRange(-100, 100)
            spin_y.valueChanged.connect(lambda v, idx=i: self.update_offset_value(idx, 'y', v))
            row_layout.addWidget(QLabel("y:"))
            row_layout.addWidget(spin_y)
            row_layout.addStretch()
            offset_layout.addLayout(row_layout)
            self.offset_spins[f"b{i}_x"] = spin_x
            self.offset_spins[f"b{i}_y"] = spin_y
        
        param_layout = QVBoxLayout()
        left_layout.addLayout(param_layout)
        
        gap_layout = QHBoxLayout()
        gap_layout.addWidget(QLabel("Band Gap:"))
        gap_layout.addWidget(self.gap_var)
        param_layout.addLayout(gap_layout)
        
        matrix_layout = QHBoxLayout()
        matrix_layout.addWidget(QLabel("Matrix Size:"))
        matrix_layout.addWidget(self.matrix_size_var)
        param_layout.addLayout(matrix_layout)
        
        fit_layout = QHBoxLayout()
        fit_layout.addWidget(self.fit_mode_screen)
        fit_layout.addWidget(self.fit_mode_actual)
        param_layout.addLayout(fit_layout)
        
        contrast_layout = QHBoxLayout()
        contrast_layout.addWidget(self.contrast_enhance_var)
        contrast_layout.addWidget(QLabel("Min:"))
        contrast_layout.addWidget(self.contrast_min_var)
        contrast_layout.addWidget(QLabel("Max:"))
        contrast_layout.addWidget(self.contrast_max_var)
        auto_btn = QPushButton("Auto")
        auto_btn.clicked.connect(self.set_auto_contrast)
        contrast_layout.addWidget(auto_btn)
        param_layout.addLayout(contrast_layout)
        
        action_layout = QHBoxLayout()
        save_btn = QPushButton("Save Parameters")
        save_btn.clicked.connect(self.save_parameters)
        action_layout.addWidget(save_btn)
        
        refresh_btn = QPushButton("Refresh")
        refresh_btn.clicked.connect(self.refresh)
        action_layout.addWidget(refresh_btn)
        
        export_btn = QPushButton("Export Image")
        export_btn.clicked.connect(self.export_current_image)
        action_layout.addWidget(export_btn)
        
        param_layout.addLayout(action_layout)
        
        left_layout.addStretch()
        
        self.display_frame = QWidget()
        display_layout = QVBoxLayout()
        self.display_frame.setLayout(display_layout)
        main_layout.addWidget(self.display_frame)
        
        self.view_tabs = QTabWidget()
        display_layout.addWidget(self.view_tabs)

        self.terminal_btn = QPushButton("Terminal ↑")
        self.terminal_btn.clicked.connect(self.toggle_terminal)
        display_layout.addWidget(self.terminal_btn)

        self.terminal_widget = TerminalWidget(self)
        # start closed using maximumHeight (animate this later). hide to avoid focus/tab stop.
        self.terminal_widget.setMaximumHeight(0)
        self.terminal_widget.hide()
        display_layout.addWidget(self.terminal_widget)

        
        self.init_view_tabs()

        self.pixel_info_box = PixelInfoBox(matrix_size_var=self.matrix_size_var, lat_lon_data=self.lat_lon_data)
        left_layout.addWidget(self.pixel_info_box)
        
        self.view_tabs.currentChanged.connect(self.on_tab_changed)
        self.frame_slider.valueChanged.connect(self.on_frame_slider_changed)
        self.frame_mode_var.buttonClicked.connect(self.update_views)
        self.rgb_frame_mode_var.buttonClicked.connect(self.preview_rgb_fusion)
    
    def cycle_view_tabs_forward(self):
        current = self.view_tabs.currentIndex()
        next_index = (current + 1) % self.view_tabs.count()
        self.view_tabs.setCurrentIndex(next_index)

    def cycle_view_tabs_backward(self):
        current = self.view_tabs.currentIndex()
        next_index = (current - 1) % self.view_tabs.count()
        self.view_tabs.setCurrentIndex(next_index)

    def zoom_in(self):
        factor = 1.25
        self._zoom_current_viewer(factor)

    def zoom_out(self):
        factor = 0.8
        self._zoom_current_viewer(factor)

    def _zoom_current_viewer(self, factor):
        current_tab = self.view_tabs.currentIndex()
        if current_tab == 0:
            viewer = self.all_bands_viewer
        elif current_tab == 3:
            viewer = self.rgb_preview_viewer
        elif current_tab == 1:
            current_band_tab = self.individual_bands_notebook.currentWidget()
            if current_band_tab:
                viewer = current_band_tab.findChild(ZoomableImageViewer)
            else:
                return
        else:
            return
        if viewer:
            old_zoom = viewer.zoom
            new_zoom = old_zoom * factor
            scroll_area = viewer.scroll_area
            viewport_center = scroll_area.viewport().rect().center()
            mouse_pos = viewport_center
            scroll_x = scroll_area.horizontalScrollBar().value()
            scroll_y = scroll_area.verticalScrollBar().value()
            mouse_image_x = (mouse_pos.x() + scroll_x) / old_zoom
            mouse_image_y = (mouse_pos.y() + scroll_y) / old_zoom
            new_scroll_x = mouse_image_x * new_zoom - mouse_pos.x()
            new_scroll_y = mouse_image_y * new_zoom - mouse_pos.y()
            viewer.zoom = new_zoom
            viewer.show_image(viewer.current_pil_image, fit_to_screen=False)
            scroll_area.horizontalScrollBar().setValue(int(new_scroll_x))
            scroll_area.verticalScrollBar().setValue(int(new_scroll_y))

    def toggle_contrast(self):
        self.contrast_enhance_var.setChecked(not self.contrast_enhance_var.isChecked())
        self.update_views()

    def toggle_fullscreen(self):
        main_window = self.main_app
        if main_window.isFullScreen():
            main_window.showNormal()
            self.left_widget.show()
        else:
            self.left_widget.hide()
            main_window.showFullScreen()

    def init_view_tabs(self):
        self.all_bands_tab = QWidget()
        all_bands_layout = QVBoxLayout()
        self.all_bands_tab.setLayout(all_bands_layout)
        self.all_bands_viewer = ZoomableImageViewer(
            pixel_info_callback=self.update_pixel_info,
            matrix_size_var=self.matrix_size_var
        )
        all_bands_layout.addWidget(self.all_bands_viewer)
        self.view_tabs.addTab(self.all_bands_tab, "All Bands")
        
        self.individual_bands_tab = QWidget()
        individual_bands_layout = QVBoxLayout()
        self.individual_bands_tab.setLayout(individual_bands_layout)
        
        self.band_checkbox_container = QWidget()
        self.band_checkbox_layout = QHBoxLayout()
        self.band_checkbox_container.setLayout(self.band_checkbox_layout)
        individual_bands_layout.addWidget(self.band_checkbox_container)
        
        self.individual_bands_notebook = QTabWidget()
        individual_bands_layout.addWidget(self.individual_bands_notebook)
        
        self.view_tabs.addTab(self.individual_bands_tab, "Individual Bands")
        
        self.histogram_tab = QWidget()
        histogram_layout = QVBoxLayout()
        self.histogram_tab.setLayout(histogram_layout)
        self.histogram_viewer = HistogramViewer()
        histogram_layout.addWidget(self.histogram_viewer)
        self.view_tabs.addTab(self.histogram_tab, "Histogram")
        
        self.fusion_tab = QWidget()
        fusion_layout = QVBoxLayout()
        self.fusion_tab.setLayout(fusion_layout)
        self.rgb_preview_viewer = ZoomableImageViewer(
            pixel_info_callback=self.update_pixel_info_rgb,
            matrix_size_var=self.matrix_size_var
        )
        fusion_layout.addWidget(self.rgb_preview_viewer)
        
        rgb_frame = QGroupBox("RGB Channel Mapping")
        rgb_frame_layout = QVBoxLayout()
        rgb_frame.setLayout(rgb_frame_layout)
        fusion_layout.addWidget(rgb_frame)
        
        red_layout = QHBoxLayout()
        red_layout.addWidget(QLabel("Red:"))
        self.red_band_var = QComboBox()
        self.red_band_var.addItems([f"b{i}" for i in range(7)])
        self.red_band_var.setCurrentText("b0")
        red_layout.addWidget(self.red_band_var)
        rgb_frame_layout.addLayout(red_layout)
        
        green_layout = QHBoxLayout()
        green_layout.addWidget(QLabel("Green:"))
        self.green_band_var = QComboBox()
        self.green_band_var.addItems([f"b{i}" for i in range(7)])
        self.green_band_var.setCurrentText("b1")
        green_layout.addWidget(self.green_band_var)
        rgb_frame_layout.addLayout(green_layout)
        
        blue_layout = QHBoxLayout()
        blue_layout.addWidget(QLabel("Blue:"))
        self.blue_band_var = QComboBox()
        self.blue_band_var.addItems([f"b{i}" for i in range(7)])
        self.blue_band_var.setCurrentText("b2")
        blue_layout.addWidget(self.blue_band_var)
        rgb_frame_layout.addLayout(blue_layout)
        
        rgb_mode_layout = QHBoxLayout()
        rgb_mode_layout.addWidget(self.rgb_frame_mode_single)
        rgb_mode_layout.addWidget(self.rgb_frame_mode_all)
        rgb_frame_layout.addLayout(rgb_mode_layout)
        
        preview_btn = QPushButton("Preview RGB Fusion")
        preview_btn.clicked.connect(self.preview_rgb_fusion)
        rgb_frame_layout.addWidget(preview_btn)
        
        self.view_tabs.addTab(self.fusion_tab, "RGB Fusion")
        
        self.help_tab = QWidget()
        help_layout = QVBoxLayout()
        self.help_tab.setLayout(help_layout)
        help_text = QTextEdit()
        help_text.setReadOnly(True)
        help_text.setPlainText("""
        XD-Band-Display-Stable Help
        Usage Instructions:
        
        1. Select a folder containing .bandXX files
        2. Use frame controls to navigate through images
        3. Adjust band offsets in the left panel
        4. Use tabs to switch between different views
        5. Click '+' button above tabs to add new dataset
        """)
        help_layout.addWidget(help_text)
        self.view_tabs.addTab(self.help_tab, "Help")
    
    def update_offset_value(self, band_idx, axis, value):
        band_key = f"b{band_idx}"
        self.band_offsets[band_key][axis] = value
        self.refresh()
    
    def select_folder(self):
        folder = QFileDialog.getExistingDirectory(self, "Select folder with .bandXX files")
        if not folder:
            return
        
        self.current_folder = folder
        self.folder_label.setText(os.path.basename(folder))
        
        param_file = os.path.join(folder, "parameters.json")
        if os.path.exists(param_file):
            self.load_parameters(folder)
        else:
            dialog = ParameterDialog(self)
            if dialog.exec_() == QDialog.Accepted:
                params = dialog.get_parameters()
                self.width_entry.setText(params["width"])
                self.height_entry.setText(params["height"])
                self.bitdepth_var.setCurrentText(str(params["bit_depth"]))
            else:
                return
        
        self.load_folder_data()
    
    def update_pixel_info(self, x, y, values, is_rgb=False):
        print("Calling update_pixel_info")
        self.pixel_info_box.update_info(x, y, values, is_rgb=is_rgb)
    
    def update_pixel_info_rgb(self, x, y, values, is_rgb=True):
        print("Calling update_pixel_info_rgb")
        self.pixel_info_box.update_info(x, y, values, is_rgb=is_rgb)
    
    def on_tab_changed(self, index):
        tab_name = self.view_tabs.tabText(index)
        current_hash = self._compute_param_hash()
        if tab_name == "All Bands":
            self._update_cached_view('all_bands', self.update_all_bands_view)
        elif tab_name == "Individual Bands":
            self._update_cached_view('individual_bands', self.update_individual_bands_view)
        elif tab_name == "Histogram":
            self.update_histogram_view()  # Histogram is cheap, no cache needed
        elif tab_name == "RGB Fusion":
            self._update_cached_view('rgb_fusion', self.preview_rgb_fusion)
        self.refresh()  # Only if needed

    def _update_cached_view(self, key, update_func):
        cache = self.view_cache.get(key, {})
        if (cache.get('frame_index') == self.current_frame_index and
            cache.get('hash') == self._compute_param_hash()):
            # Use cache
            self._apply_cached_image(key, cache['pil_image'], cache['original_data'])
            return
        # Update and cache
        update_func()
        # After update_func sets viewer.image and original_image_data, cache them
        viewer = self._get_viewer_for_key(key)
        if viewer:
            self.view_cache[key] = {
                'frame_index': self.current_frame_index,
                'pil_image': viewer.image.copy(),  # Shallow copy
                'original_data': viewer.original_image_data.copy(),
                'hash': self._compute_param_hash()
            }
            
    def save_parameters(self):
        if not self.current_folder:
            QMessageBox.critical(self, "Error", "No folder selected. Please select a folder first.")
            return
        
        params = {
            "width": self.width_entry.text(),
            "height": self.height_entry.text(),
            "bit_depth": int(self.bitdepth_var.currentText()),
            "band_gap": self.gap_var.value(),
            "matrix_size": self.matrix_size_var.value(),
            "contrast_enhance": self.contrast_enhance_var.isChecked(),
            "contrast_min": self.contrast_min_var.value(),
            "contrast_max": self.contrast_max_var.value(),
            "band_enabled": {key: self.band_enabled.get(key, QCheckBox()).isChecked() for key in self.band_frames if self.band_frames[key] is not None},
            "rgb_channels": {
                "red": self.red_band_var.currentText(),
                "green": self.green_band_var.currentText(),
                "blue": self.blue_band_var.currentText()
            },
            "band_offsets": self.band_offsets
        }
        
        try:
            with open(os.path.join(self.current_folder, "parameters.json"), "w") as f:
                json.dump(params, f, indent=4)
            QMessageBox.information(self, "Success", "Parameters saved successfully")
        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to save parameters: {e}")
    
    def _invalidate_cache(self):
        self.view_cache.clear()
        self.param_hash = self._compute_param_hash()
        self.update_views()
        
    def apply_contrast_enhancement(self, frame):
        if not self.contrast_enhance_var.isChecked():
            return frame
        
        min_val = self.histogram_viewer.min_val
        max_val = self.histogram_viewer.max_val
        
        if max_val <= min_val:
            min_val = float(np.min(frame))
            max_val = float(np.max(frame))
        
        if self.contrast_min_var.value() != 0.0:
            min_val = self.contrast_min_var.value()
        if self.contrast_max_var.value() != 255.0:
            max_val = self.contrast_max_var.value()
        
        if max_val == min_val:
            return frame
        
        enhanced = ((frame.astype(np.float32) - min_val) / (max_val - min_val) * 255)
        return np.clip(enhanced, 0, 255).astype(np.uint8)
    
    def load_parameters(self, folder):
        param_file = os.path.join(folder, "parameters.json")
        if not os.path.exists(param_file):
            return
        
        try:
            with open(param_file, "r") as f:
                params = json.load(f)
            
            self.width_entry.setText(params.get("width", "8448"))
            self.height_entry.setText(params.get("height", "384"))
            self.bitdepth_var.setCurrentText(str(params.get("bit_depth", 10)))
            self.gap_var.setValue(params.get("band_gap", 10))
            self.matrix_size_var.setValue(params.get("matrix_size", 3))
            self.contrast_enhance_var.setChecked(params.get("contrast_enhance", False))
            self.contrast_min_var.setValue(params.get("contrast_min", 0.0))
            self.contrast_max_var.setValue(params.get("contrast_max", 255.0))
            
            band_enabled = params.get("band_enabled", {})
            for key in band_enabled:
                if key in self.band_enabled:
                    self.band_enabled[key].setChecked(band_enabled[key])
            
            rgb_channels = params.get("rgb_channels", {})
            self.red_band_var.setCurrentText(rgb_channels.get("red", "b0"))
            self.green_band_var.setCurrentText(rgb_channels.get("green", "b1"))
            self.blue_band_var.setCurrentText(rgb_channels.get("blue", "b2"))
            
            band_offsets = params.get("band_offsets", {})
            for i in range(7):
                band_key = f"b{i}"
                offsets = band_offsets.get(band_key, {"x": 0, "y": 0})
                self.band_offsets[band_key]["x"] = offsets["x"]
                self.band_offsets[band_key]["y"] = offsets["y"]
                self.offset_spins[f"{band_key}_x"].setValue(offsets["x"])
                self.offset_spins[f"{band_key}_y"].setValue(offsets["y"])
            
        except Exception as e:
            QMessageBox.warning(self, "Warning", f"Failed to load parameters: {e}")
    
    def refresh(self):
        self.update_views()
        if self.fit_mode_var.checkedId() == 0:
            self.fit_to_screen()
    
    def set_auto_contrast(self):
        if not self.band_frames:
            return

        # Compute min/max across all bands for the current frame
        min_val = float('inf')
        max_val = float('-inf')
        keys = [k for k in sorted(self.band_frames.keys()) if self.band_enabled.get(k, False)]
        for key in keys:
            frames = self.band_frames.get(key)
            if frames and self.current_frame_index < len(frames):
                frame = frames[self.current_frame_index]
                min_val = min(min_val, float(np.min(frame)))
                max_val = max(max_val, float(np.max(frame)))

        if min_val == float('inf') or max_val == float('-inf'):
            min_val = 0.0
            max_val = 255.0

        self.contrast_min_var.setValue(min_val)
        self.contrast_max_var.setValue(max_val)
        self.histogram_viewer.min_val = min_val
        self.histogram_viewer.max_val = max_val
        self.refresh()
    
    def load_folder_data(self):
        try:
            width = int(self.width_entry.text())
            height = int(self.height_entry.text())
            self.bitdepth = int(self.bitdepth_var.currentText())
            if width <= 0 or height <= 0:
                raise ValueError("Width and height must be positive integers")
        except ValueError as e:
            QMessageBox.critical(self, "Error", str(e))
            return

        # find .meta file (first one found)
        meta_file = None
        try:
            for f in os.listdir(self.current_folder):
                if f.endswith(".meta"):
                    meta_file = os.path.join(self.current_folder, f)
                    break
        except Exception as e:
            print(f"Error listing folder {self.current_folder}: {e}")

        # parse .meta for center lat/lon (parse_meta_file should be defined elsewhere)
        center_lat, center_lon = None, None
        if meta_file:
            try:
                center_lat, center_lon = parse_meta_file(meta_file)
            except Exception as e:
                print(f"Error parsing meta file {meta_file}: {e}")

        # fallback to 0,0 if parsing failed (but still compute grid)
        if center_lat is not None and center_lon is not None:
            clat, clon = center_lat, center_lon
        else:
            print("Warning: Could not parse .meta file or .meta missing; defaulting center to (0.0, 0.0)")
            clat, clon = 0.0, 0.0

        # compute lat/lon grid for all pixels (calculate_pixel_coordinates expected to return (lat_grid, lon_grid))
        try:
            self.lat_lon_data = calculate_pixel_coordinates(clat, clon, width, height)
        except Exception as e:
            print(f"Error computing lat/lon grid: {e}")
            # make a safe placeholder so callers won't crash
            self.lat_lon_data = (_np.zeros((height, width)), _np.zeros((height, width)))

        # attach geo_info and lat_lon_data to viewers and pixel info so mouse handlers can access them
        # pixel_size_m is a safe default; change if you compute it elsewhere
        pixel_size_m = getattr(self, "pixel_size_m", 1.0)
        self.geo_info = (clat, clon, width, height, pixel_size_m)

        # robust assignments (silent if attribute doesn't exist)
        try:
            self.pixel_info_box.lat_lon_data = self.lat_lon_data
            self.pixel_info_box.geo_info = self.geo_info
        except Exception:
            pass
        try:
            self.all_bands_viewer.lat_lon_data = self.lat_lon_data
            self.all_bands_viewer.geo_info = self.geo_info
            # nested image_label (if present)
            if hasattr(self.all_bands_viewer, "image_label"):
                self.all_bands_viewer.image_label.lat_lon_data = self.lat_lon_data
                self.all_bands_viewer.image_label.geo_info = self.geo_info
        except Exception:
            pass
        try:
            self.rgb_preview_viewer.lat_lon_data = self.lat_lon_data
            self.rgb_preview_viewer.geo_info = self.geo_info
            if hasattr(self.rgb_preview_viewer, "image_label"):
                self.rgb_preview_viewer.image_label.lat_lon_data = self.lat_lon_data
                self.rgb_preview_viewer.image_label.geo_info = self.geo_info
        except Exception:
            pass

        # Print quick sanity check of corners (safe try)
        try:
            lat_grid, lon_grid = self.lat_lon_data
            print(f"Top-Left: {lat_grid[0,0]:.8f}, {lon_grid[0,0]:.8f}")
            print(f"Bottom-Right: {lat_grid[height-1, width-1]:.8f}, {lon_grid[height-1, width-1]:.8f}")
        except Exception:
            print("Lat/lon grid not printable (shape mismatch or error)")

        # --- ProcMode extraction: try JSON first, then LOG; be defensive ---
        proc_mode, base_name = None, None
        try:
            for f in os.listdir(self.current_folder):
                if f.endswith(".json") and f != "parameters.json":
                    with open(os.path.join(self.current_folder, f), "r") as jf:
                        try:
                            config = json.load(jf)
                        except Exception:
                            config = {}
                        proc_mode = config.get("ProcMode")
                        base_name = os.path.splitext(f)[0]
                    if proc_mode:
                        break
        except Exception as e:
            print(f"Error reading JSON files: {e}")

        if not proc_mode:
            try:
                for f in os.listdir(self.current_folder):
                    if f.endswith(".log"):
                        with open(os.path.join(self.current_folder, f), "r") as lf:
                            for line in lf:
                                if "Arguments received from parameter file" in line:
                                    # try to extract the proc_mode fragment after 'file' (keeps previous behavior)
                                    proc_mode = line.split("file")[-1].strip(": \n")
                                    base_name = os.path.splitext(f)[0]
                                    break
                        if proc_mode:
                            break
            except Exception as e:
                print(f"Error reading LOG files: {e}")

        if not proc_mode:
            QMessageBox.critical(self, "Error", "Could not extract ProcMode from JSON or LOG")
            return

        # try to parse band_selection & binning from proc_mode defensively
        band_selection = None
        binning = None
        try:
            parts = proc_mode.strip().split()
            # try original indexes first (old format)
            if len(parts) > 12:
                band_selection = int(parts[6])
                binning = int(parts[12])
            else:
                # fallback: try to find numeric tokens in the string and pick plausible values
                nums = [int(tok) for tok in parts if tok.isdigit()]
                if len(nums) >= 2:
                    band_selection, binning = nums[0], nums[1]
        except Exception:
            pass

        if band_selection is None or binning is None:
            QMessageBox.critical(self, "Error", "Invalid ProcMode format (cannot determine band selection / binning)")
            return

        # store base name and update UI tab title if applicable
        self.base_name = base_name
        if self.main_app:
            self.main_app.update_tab_name(self, os.path.basename(self.current_folder))

        # load bands (lazy)
        self.band_frames = {}
        files_checked = []
        for i in range(7):
            # skip disabled bands
            if not ((band_selection >> i) & 1):
                continue

            is_binned = (binning >> i) & 1
            try:
                if is_binned:
                    fname = os.path.join(self.current_folder, f"{base_name}.band{i}2")
                    files_checked.append(fname)
                    if os.path.exists(fname) and os.path.getsize(fname) > 0:
                        # binned: assume half resolution in both dims
                        self.band_frames[f"b{i}_binned"] = LazyFrames(fname, width // 2, height // 2, self.bitdepth)
                else:
                    lfile = os.path.join(self.current_folder, f"{base_name}.band{i}0")
                    rfile = os.path.join(self.current_folder, f"{base_name}.band{i}1")
                    files_checked.extend([lfile, rfile])
                    if os.path.exists(lfile) and os.path.getsize(lfile) > 0:
                        self.band_frames[f"b{i}_left"] = LazyFrames(lfile, width // 2, height, self.bitdepth)
                    if os.path.exists(rfile) and os.path.getsize(rfile) > 0:
                        self.band_frames[f"b{i}_right"] = LazyFrames(rfile, width // 2, height, self.bitdepth)
            except Exception as e:
                print(f"Band {i} error: {e}")
                continue

        # create checkboxes for loaded bands (keep them checked by default)
        self.band_enabled = {key: QCheckBox() for key in self.band_frames}
        for key, cb in self.band_enabled.items():
            cb.setChecked(True)

        # if nothing loaded, show warning and a placeholder image
        if not self.band_frames:
            QMessageBox.warning(self, "Warning", f"No valid band frames loaded. Checked files: {', '.join(files_checked)}")
            placeholder = Image.fromarray(np.zeros((height, width // 2), dtype=np.uint8))
            try:
                self.all_bands_viewer.show_image(placeholder, fit_to_screen=(self.fit_mode_var.checkedId() == 0))
            except Exception:
                pass
            return

        # update RGB selectors
        avail_keys = sorted(self.band_frames.keys())
        self.red_band_var.clear()
        self.red_band_var.addItems(avail_keys)
        self.green_band_var.clear()
        self.green_band_var.addItems(avail_keys)
        self.blue_band_var.clear()
        self.blue_band_var.addItems(avail_keys)

        # refresh UI controls / view
        try:
            self.update_views()
        except Exception as e:
            print(f"update_views() error: {e}")
        try:
            self.update_frame_controls()
        except Exception as e:
            print(f"update_frame_controls() error: {e}")

        # apply fit to screen if requested
        try:
            if self.fit_mode_var.checkedId() == 0:
                self.fit_to_screen()
        except Exception:
            pass

        # set contrast autoscaling if available
        try:
            self.set_auto_contrast()
        except Exception:
            pass


    def toggle_terminal(self):
        try:
            current_max = self.terminal_widget.maximumHeight()
            target = int(self.height() * 0.30) if current_max == 0 else 0

            if target > 0 and not self.terminal_widget.isVisible():
                self.terminal_widget.setMaximumHeight(0)
                self.terminal_widget.show()

            anim = QPropertyAnimation(self.terminal_widget, b"maximumHeight")
            anim.setDuration(250)
            anim.setStartValue(current_max)
            anim.setEndValue(target)

            def on_finished():
                if target == 0:
                    try:
                        self.terminal_widget.hide()
                    except Exception:
                        pass
                else:
                    # give focus to inline input after terminal is shown
                    try:
                        self.terminal_widget.focus_input()
                    except Exception:
                        pass
                try:
                    self.terminal_btn.setText("Terminal ↓" if target > 0 else "Terminal ↑")
                except Exception:
                    pass

            anim.finished.connect(on_finished)
            self._terminal_anim = anim
            anim.start()
        except Exception as e:
            print(f"Terminal animation error: {e}")
            # fallback: immediate toggle
            if self.terminal_widget.isVisible() and self.terminal_widget.maximumHeight() > 0:
                self.terminal_widget.setMaximumHeight(0)
                self.terminal_widget.hide()
                try:
                    self.terminal_btn.setText("Terminal ↑")
                except Exception:
                    pass
            else:
                self.terminal_widget.setMaximumHeight(int(self.height() * 0.3))
                self.terminal_widget.show()
                try:
                    self.terminal_btn.setText("Terminal ↓")
                except Exception:
                    pass



    def export_current_image(self):
        if not self.current_folder or not self.base_name:
            QMessageBox.critical(self, "Error", "No image loaded. Please select a folder first.")
            return
        
        current_tab = self.view_tabs.currentIndex()
        if current_tab == 0 and hasattr(self.all_bands_viewer, 'image') and self.all_bands_viewer.image:
            image= self.all_bands_viewer.image
            tab_suffix = "all_bands"
        elif current_tab == 1:
            current_band_tab = self.individual_bands_notebook.currentIndex()
            band_key = f"b{self.individual_bands_notebook.tabText(current_band_tab).split()[1]}"
            viewer = self.individual_bands_notebook.widget(current_band_tab).findChild(ZoomableImageViewer)
            if viewer and hasattr(viewer, 'image') and viewer.image:
                image = viewer.image
                tab_suffix = f"individual_bands_{band_key}"
            else:
                QMessageBox.critical(self, "Error", "No image available in Individual Bands tab")
                return
        elif current_tab == 3 and hasattr(self.rgb_preview_viewer, 'image') and self.rgb_preview_viewer.image:
            image = self.rgb_preview_viewer.image
            tab_suffix = "rgb"
        elif current_tab == 2:
            QMessageBox.critical(self, "Error", "Histogram export is not supported")
            return
        else:
            QMessageBox.critical(self, "Error", "No image available to export")
            return
        
        default_filename = f"{self.base_name}_{tab_suffix}.png"
        filename, _ = QFileDialog.getSaveFileName(
            self, "Save Current Image As", default_filename,
            "PNG (*.png);;JPEG (*.jpg);;TIFF (*.tif);;All Files (*)"
        )
        
        if filename:
            try:
                image.save(filename)
                QMessageBox.information(self, "Success", f"Image exported to {filename}")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to export image: {e}")
    
    def update_views(self):
        try:
            self.update_all_bands_view()
            self.update_individual_bands_view()
            self.update_histogram_view()
            self.preview_rgb_fusion()
        except Exception as e:
            print(f"Error in update_views: {e}")
    
    def update_all_bands_view(self):
        if not self.band_frames:
            self.all_bands_viewer.show_image(None)
            return
        
        keys = [k for k in sorted(self.band_frames.keys()) if self.band_enabled.get(k, False) and self.band_frames[k] is not None]
        if not keys:
            self.all_bands_viewer.show_image(None)
            return
        
        # Do not set lat_lon_data for all_bands as it's stacked
        # self.all_bands_viewer.lat_lon_data = self.lat_lon_data
        frames = self.band_frames[keys[0]]
        if self.current_frame_index >= len(frames):
            self.current_frame_index = 0
        
        binned_keys = [k for k in keys if not k.endswith(('_left', '_right'))]
        unbinned_left_keys = [k for k in keys if k.endswith('_left')]
        unbinned_right_keys = [k for k in keys if k.endswith('_right')]
        
        parts = []
        original_parts = []
        max_width = 0
        if binned_keys:
            max_width = max(self.band_frames[key].w for key in binned_keys)
        if unbinned_left_keys or unbinned_right_keys:
            unbinned_w = max(self.band_frames[k].w for k in unbinned_left_keys + unbinned_right_keys)
            max_width = max(max_width, unbinned_w)
        
        # Binned bands
        for idx, key in enumerate(binned_keys):
            frame = self.band_frames[key][self.current_frame_index]
            frame = self.apply_offset(frame, key)
            if frame.shape[1] < max_width:
                padding = np.zeros((frame.shape[0], max_width - frame.shape[1]), dtype=np.uint8)
                original_padded = np.hstack([frame, padding])
                original_parts.append(original_padded)
            else:
                original_parts.append(frame)
            display_frame = self.apply_contrast_enhancement(frame) if self.contrast_enhance_var.isChecked() else frame
            
            if display_frame.shape[1] < max_width:
                padding = np.zeros((display_frame.shape[0], max_width - display_frame.shape[1]), dtype=np.uint8)
                display_frame = np.hstack([display_frame, padding])
            
            if idx < len(binned_keys) - 1 or unbinned_left_keys or unbinned_right_keys:
                gap = np.zeros((self.gap_var.value(), display_frame.shape[1]), dtype=np.uint8)
                display_frame = np.vstack([display_frame, gap])
            
            parts.append(display_frame)
            del frame, display_frame
        
        # Unbinned left
        for idx, key in enumerate(unbinned_left_keys):
            base_key = key.rsplit('_', 1)[0]
            frame = self.band_frames[key][self.current_frame_index]
            frame = self.apply_offset(frame, base_key)
            if frame.shape[1] < max_width:
                padding = np.zeros((frame.shape[0], max_width - frame.shape[1]), dtype=np.uint8)
                original_padded = np.hstack([frame, padding])
                original_parts.append(original_padded)
            else:
                original_parts.append(frame)
            display_frame = self.apply_contrast_enhancement(frame) if self.contrast_enhance_var.isChecked() else frame
            
            if display_frame.shape[1] < max_width:
                padding = np.zeros((display_frame.shape[0], max_width - display_frame.shape[1]), dtype=np.uint8)
                display_frame = np.hstack([display_frame, padding])
            
            if idx < len(unbinned_left_keys) - 1 or unbinned_right_keys:
                gap = np.zeros((self.gap_var.value(), display_frame.shape[1]), dtype=np.uint8)
                display_frame = np.vstack([display_frame, gap])
            
            parts.append(display_frame)
            del frame, display_frame
        
        # Unbinned right
        for idx, key in enumerate(unbinned_right_keys):
            base_key = key.rsplit('_', 1)[0]
            frame = self.band_frames[key][self.current_frame_index]
            frame = self.apply_offset(frame, base_key)
            if frame.shape[1] < max_width:
                padding = np.zeros((frame.shape[0], max_width - frame.shape[1]), dtype=np.uint8)
                original_padded = np.hstack([frame, padding])
                original_parts.append(original_padded)
            else:
                original_parts.append(frame)
            display_frame = self.apply_contrast_enhancement(frame) if self.contrast_enhance_var.isChecked() else frame
            
            if display_frame.shape[1] < max_width:
                padding = np.zeros((display_frame.shape[0], max_width - display_frame.shape[1]), dtype=np.uint8)
                display_frame = np.hstack([display_frame, padding])
            
            if idx < len(unbinned_right_keys) - 1:
                gap = np.zeros((self.gap_var.value(), display_frame.shape[1]), dtype=np.uint8)
                display_frame = np.vstack([display_frame, gap])
            
            parts.append(display_frame)
            del frame, display_frame
        
        if parts:
            original_full = np.vstack(original_parts)
            full = np.vstack(parts)
            pil = Image.fromarray(full)
            self.all_bands_viewer.original_image_data = original_full
            self.all_bands_viewer.show_image(pil, fit_to_screen=(self.fit_mode_var.checkedId() == 0))
            print(f"Updated all bands view: original_full shape: {original_full.shape}")
            del full, original_full, pil
        else:
            self.all_bands_viewer.show_image(None)
        gc.collect()
    
    def update_individual_bands_view(self):
        while self.individual_bands_notebook.count():
            widget = self.individual_bands_notebook.widget(0)
            self.individual_bands_notebook.removeTab(0)
            widget.deleteLater()
        
        while self.band_checkbox_layout.count():
            item = self.band_checkbox_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        
        if not self.band_frames:
            return
        
        for key in sorted(self.band_frames.keys()):
            cb = QCheckBox(f"Band {key[1:]}")
            cb.setChecked(self.band_enabled.get(key, QCheckBox()).isChecked())
            cb.stateChanged.connect(lambda state, k=key: self.toggle_band(k, state))
            self.band_checkbox_layout.addWidget(cb)
        self.band_checkbox_layout.addStretch()
        
        keys = [k for k in sorted(self.band_frames.keys()) if self.band_enabled.get(k, False) and self.band_frames[k] is not None]
        
        if self.frame_mode_var.checkedId() == 0:
            for key in keys:
                self._create_band_tab(key)
        else:
            start_frame = self.start_frame_entry.value() - 1
            end_frame = self.end_frame_entry.value() - 1
            for key in keys:
                self._create_band_tab(key, start_frame, end_frame)
        
        unbinned_keys = [k for k in keys if k.endswith(('_left', '_right'))]
        if unbinned_keys:
            self._create_pan_tab(unbinned_keys)
        gc.collect()

    def _create_band_tab(self, key, start_frame=None, end_frame=None):
        tab = QWidget()
        layout = QVBoxLayout()
        tab.setLayout(layout)
        
        viewer = ZoomableImageViewer(
            pixel_info_callback=self.update_pixel_info,
            matrix_size_var=self.matrix_size_var,
            lat_lon_data=self.lat_lon_data
        )
        layout.addWidget(viewer)
        
        frames = self.band_frames[key]
        base_key = key.rsplit('_', 1)[0] if '_' in key else key
        
        if start_frame is None:
            frame = frames[self.current_frame_index]
            frame = self.apply_offset(frame, base_key)
            viewer.original_image_data = frame
            display_frame = self.apply_contrast_enhancement(frame) if self.contrast_enhance_var.isChecked() else frame
            pil_image = Image.fromarray(display_frame)
            viewer.show_image(pil_image, fit_to_screen=(self.fit_mode_var.checkedId() == 0))
            print(f"Created band tab {key}: frame shape: {frame.shape}")
            del frame, display_frame, pil_image
        else:
            parts = []
            original_parts = []
            for i in range(start_frame, end_frame + 1):
                if i < len(frames):
                    frame = frames[i]
                    frame = self.apply_offset(frame, base_key)
                    original_parts.append(frame)
                    display_frame = self.apply_contrast_enhancement(frame) if self.contrast_enhance_var.isChecked() else frame
                    if i < end_frame:
                        gap = np.zeros((self.gap_var.value(), display_frame.shape[1]), dtype=np.uint8)
                        display_frame = np.vstack([display_frame, gap])
                    parts.append(display_frame)
                    del frame, display_frame
            if parts:
                original_full = np.vstack(original_parts)
                full = np.vstack(parts)
                pil_image = Image.fromarray(full)
                viewer.original_image_data = original_full
                viewer.show_image(pil_image, fit_to_screen=(self.fit_mode_var.checkedId() == 0))
                print(f"Created band tab {key} (range): original_full shape: {original_full.shape}")
                del original_full, full, pil_image, parts, original_parts
        
        self.individual_bands_notebook.addTab(tab, f"Band {base_key[1:]}")
        gc.collect()

    def _create_pan_tab(self, unbinned_keys):
        pan_tab = QWidget()
        layout = QVBoxLayout()
        pan_tab.setLayout(layout)
        
        viewer = ZoomableImageViewer(
            pixel_info_callback=self.update_pixel_info,
            matrix_size_var=self.matrix_size_var,
            lat_lon_data=self.lat_lon_data
        )
        layout.addWidget(viewer)
        
        if self.frame_mode_var.checkedId() == 0:
            parts = []
            original_parts = []
            base_keys = sorted(set(k.rsplit('_', 1)[0] for k in unbinned_keys))
            for base_key in base_keys:
                left_key = f"{base_key}_left"
                right_key = f"{base_key}_right"
                if left_key in self.band_frames and right_key in self.band_frames:
                    left_frame = self.band_frames[left_key][self.current_frame_index]
                    right_frame = self.band_frames[right_key][self.current_frame_index]
                    left_frame = self.apply_offset(left_frame, base_key)
                    right_frame = self.apply_offset(right_frame, base_key)
                    original_full_frame = np.hstack([left_frame, right_frame])
                    original_parts.append(original_full_frame)
                    display_left = self.apply_contrast_enhancement(left_frame) if self.contrast_enhance_var.isChecked() else left_frame
                    display_right = self.apply_contrast_enhancement(right_frame) if self.contrast_enhance_var.isChecked() else right_frame
                    display_full_frame = np.hstack([display_left, display_right])
                    parts.append(display_full_frame)
                    del left_frame, right_frame, display_left, display_right, original_full_frame, display_full_frame
            
            if parts:
                original_full = np.vstack(original_parts)
                full = np.vstack(parts)
                pil_image = Image.fromarray(full)
                viewer.original_image_data = original_full
                viewer.show_image(pil_image, fit_to_screen=(self.fit_mode_var.checkedId() == 0))
                print(f"Created pan tab: original_full shape: {original_full.shape}")
                del original_full, full, pil_image, parts, original_parts
        else:
            start_frame = self.start_frame_entry.value() - 1
            end_frame = self.end_frame_entry.value() - 1
            parts = []
            original_parts = []
            base_keys = sorted(set(k.rsplit('_', 1)[0] for k in unbinned_keys))
            
            for i in range(start_frame, end_frame + 1):
                frame_parts = []
                frame_original_parts = []
                for base_key in base_keys:
                    left_key = f"{base_key}_left"
                    right_key = f"{base_key}_right"
                    if left_key in self.band_frames and right_key in self.band_frames:
                        left_frame = self.band_frames[left_key][i]
                        right_frame = self.band_frames[right_key][i]
                        left_frame = self.apply_offset(left_frame, base_key)
                        right_frame = self.apply_offset(right_frame, base_key)
                        original_full_frame = np.hstack([left_frame, right_frame])
                        frame_original_parts.append(original_full_frame)
                        display_left = self.apply_contrast_enhancement(left_frame) if self.contrast_enhance_var.isChecked() else left_frame
                        display_right = self.apply_contrast_enhancement(right_frame) if self.contrast_enhance_var.isChecked() else right_frame
                        display_full_frame = np.hstack([display_left, display_right])
                        frame_parts.append(display_full_frame)
                        del left_frame, right_frame, display_left, display_right, original_full_frame, display_full_frame
                
                if frame_parts:
                    frame_full = np.vstack(frame_parts)
                    frame_original_full = np.vstack(frame_original_parts)
                    if i < end_frame:
                        gap = np.zeros((self.gap_var.value(), frame_full.shape[1]), dtype=np.uint8)
                        frame_full = np.vstack([frame_full, gap])
                        frame_original_full = np.vstack([frame_original_full, gap])
                    parts.append(frame_full)
                    original_parts.append(frame_original_full)
                    del frame_full, frame_original_full, frame_parts, frame_original_parts
            
            if parts:
                original_full = np.vstack(original_parts)
                full = np.vstack(parts)
                pil_image = Image.fromarray(full)
                viewer.original_image_data = original_full
                viewer.show_image(pil_image, fit_to_screen=(self.fit_mode_var.checkedId() == 0))
                print(f"Created pan tab (range): original_full shape: {original_full.shape}")
                del original_full, full, pil_image, parts, original_parts
            else:
                viewer.show_image(None)
        
        self.individual_bands_notebook.addTab(pan_tab, "Pan")
        gc.collect()


    def toggle_band(self, key, state):
        if key in self.band_enabled:
            self.band_enabled[key].setChecked(state)
        self.update_individual_bands_view()

    def update_histogram_view(self):
        if not self.band_frames:
            return
        
        keys = sorted(self.band_frames.keys())
        frames = self.band_frames[keys[0]] if keys else None
        if frames is None or self.current_frame_index >= len(frames):
            self.current_frame_index = 0
        
        if self.histogram_viewer.single_frame_radio.isChecked():
            self.histogram_viewer.update_histogram({k: self.band_frames[k] for k in keys if self.band_frames[k] is not None}, self.current_frame_index, "Single")
        else:
            try:
                start_frame = self.start_frame_entry.value() - 1
                end_frame = self.end_frame_entry.value() - 1
                if start_frame < 0 or end_frame >= len(frames) or start_frame > end_frame:
                    return
                self.histogram_viewer.update_histogram({k: self.band_frames[k] for k in keys if self.band_frames[k] is not None}, self.current_frame_index, "Range", start_frame, end_frame)
            except ValueError:
                return
        gc.collect()
    
    def update_frame_controls(self):
        if not self.band_frames:
            self.frame_slider.setRange(0, 0)
            self.frame_label.setText("0/0")
            self.start_frame_entry.setValue(1)
            self.end_frame_entry.setValue(1)
            return
        
        keys = sorted(k for k in self.band_frames.keys() if self.band_frames[k] is not None)
        if not keys:
            return
        frames = self.band_frames[keys[0]]
        max_frames = len(frames)
        
        self.frame_slider.setRange(0, max_frames-1)
        self.frame_label.setText(f"{self.current_frame_index+1}/{max_frames}")
        self.start_frame_entry.setRange(1, max_frames)
        self.start_frame_entry.setValue(1)
        self.end_frame_entry.setRange(1, max_frames)
        self.end_frame_entry.setValue(max_frames)
    
    def on_frame_slider_changed(self, value):
        self.current_frame_index = value
        self.update_views()
        self.frame_label.setText(f"{self.current_frame_index+1}/{self.frame_slider.maximum()+1}")
        if self.fit_mode_var.checkedId() == 0:
            self.fit_to_screen()
    
    def change_frame(self, delta):
        new_index = self.current_frame_index + delta
        max_frames = self.frame_slider.maximum() + 1 if self.band_frames else 0
        
        if max_frames > 0:
            if new_index < 0:
                new_index = 0
            elif new_index >= max_frames:
                new_index = max_frames - 1
            
            self.current_frame_index = new_index
            self.frame_slider.setValue(new_index)
            self.update_views()
            if self.fit_mode_var.checkedId() == 0:
                self.fit_to_screen()
    
    def toggle_play(self):
        if not self.band_frames:
            return
        
        self.playing = not self.playing
        if self.playing:
            self.play_btn.setText("⏸ Pause")
            self.play_next_frame()
        else:
            self.play_btn.setText("▶ Play")
    
    def play_next_frame(self):
        if not self.playing or not self.band_frames:
            self.play_btn.setText("▶ Play")
            self.playing = False
            return
        
        max_frames = self.frame_slider.maximum() + 1
        if self.current_frame_index < max_frames - 1:
            self.change_frame(1)
        else:
            self.current_frame_index = 0
            self.frame_slider.setValue(0)
            self.update_views()
        
        QTimer.singleShot(self.play_delay, self.play_next_frame)
    
    def apply_offset(self, frame, band_key):
        x_offset = self.band_offsets.get(band_key, {"x": 0})["x"]
        y_offset = self.band_offsets.get(band_key, {"y": 0})["y"]
        
        if x_offset == 0 and y_offset == 0:
            return frame
        
        h, w = frame.shape
        result = np.zeros_like(frame)
        
        x_start = max(0, x_offset)
        x_end = min(w, w + x_offset)
        y_start = max(0, y_offset)
        y_end = min(h, h + y_offset)
        
        src_x_start = max(0, -x_offset)
        src_x_end = min(w, w - x_offset)
        src_y_start = max(0, -y_offset)
        src_y_end = min(h, h - y_offset)
        
        if x_end > x_start and y_end > y_start and src_x_end > src_x_start and src_y_end > src_y_start:
            result[y_start:y_end, x_start:x_end] = frame[src_y_start:src_y_end, src_x_start:src_x_end]
        
        return result
    
    def preview_rgb_fusion(self):
        if not self.band_frames:
            self.rgb_preview_viewer.show_image(None)
            return
        self.rgb_preview_viewer.image_label.lat_lon_data = self.lat_lon_data
        self.rgb_bands["R"] = self.red_band_var.currentText()
        self.rgb_bands["G"] = self.green_band_var.currentText()
        self.rgb_bands["B"] = self.blue_band_var.currentText()
        
        keys = sorted(self.band_frames.keys())
        frames = self.band_frames.get(keys[0]) if keys else None
        if frames is None or self.current_frame_index >= len(frames):
            self.current_frame_index = 0
        
        rgb_mode = "Single" if self.rgb_frame_mode_var.checkedId() == 0 else "All"
        
        try:
            if rgb_mode == "Single":
                rgb_original = []
                rgb_display = []
                for channel in ["R", "G", "B"]:
                    band_key = self.rgb_bands[channel]
                    base_key = band_key.rsplit('_', 1)[0] if '_' in band_key else band_key
                    if band_key in self.band_frames and self.band_frames[band_key] is not None and self.current_frame_index < len(self.band_frames[band_key]):
                        frame = self.band_frames[band_key][self.current_frame_index]
                        frame = self.apply_offset(frame, base_key)
                        rgb_original.append(frame)
                        display_frame = self.apply_contrast_enhancement(frame) if self.contrast_enhance_var.isChecked() else frame
                        rgb_display.append(display_frame)
                    else:
                        zero = np.zeros((self.band_frames[keys[0]].h if keys else 384, self.band_frames[keys[0]].w if keys else 4224), dtype=np.uint8)
                        rgb_original.append(zero)
                        rgb_display.append(zero)
                
                rgb_array_original = np.stack(rgb_original, axis=-1)
                rgb_array_display = np.stack(rgb_display, axis=-1)
                pil_image = Image.fromarray(rgb_array_display)
                self.rgb_preview_viewer.original_image_data = rgb_array_original
                self.rgb_preview_viewer.show_image(pil_image, fit_to_screen=(self.fit_mode_var.checkedId() == 0))
                print(f"RGB fusion (single): rgb_array shape: {rgb_array_original.shape}")
                del rgb_original, rgb_display, rgb_array_original, rgb_array_display, pil_image
            else:
                start_frame = self.start_frame_entry.value() - 1
                end_frame = self.end_frame_entry.value() - 1
                if start_frame < 0 or end_frame >= len(frames) or start_frame > end_frame:
                    self.rgb_preview_viewer.show_image(None)
                    return
                
                parts = []
                original_parts = []
                for i in range(start_frame, end_frame + 1):
                    rgb_original = []
                    rgb_display = []
                    for channel in ["R", "G", "B"]:
                        band_key = self.rgb_bands[channel]
                        base_key = band_key.rsplit('_', 1)[0] if '_' in band_key else band_key
                        if band_key in self.band_frames and self.band_frames[band_key] is not None and i < len(self.band_frames[band_key]):
                            frame = self.band_frames[band_key][i]
                            frame = self.apply_offset(frame, base_key)
                            rgb_original.append(frame)
                            display_frame = self.apply_contrast_enhancement(frame) if self.contrast_enhance_var.isChecked() else frame
                            rgb_display.append(display_frame)
                        else:
                            zero = np.zeros((self.band_frames[keys[0]].h if keys else 384, self.band_frames[keys[0]].w if keys else 4224), dtype=np.uint8)
                            rgb_original.append(zero)
                            rgb_display.append(zero)
                    
                    rgb_array_original = np.stack(rgb_original, axis=-1)
                    rgb_array_display = np.stack(rgb_display, axis=-1)
                    original_parts.append(rgb_array_original)
                    if i < end_frame:
                        gap = np.zeros((self.gap_var.value(), rgb_array_display.shape[1], 3), dtype=np.uint8)
                        rgb_array_display = np.vstack([rgb_array_display, gap])
                    parts.append(rgb_array_display)
                    del rgb_original, rgb_display, rgb_array_original, rgb_array_display
                
                if parts:
                    original_full = np.vstack(original_parts)
                    full = np.vstack(parts)
                    pil_image = Image.fromarray(full)
                    self.rgb_preview_viewer.original_image_data = original_full
                    self.rgb_preview_viewer.show_image(pil_image, fit_to_screen=(self.fit_mode_var.checkedId() == 0))
                    print(f"RGB fusion (range): original_full shape: {original_full.shape}")
                    del original_full, full, pil_image, parts, original_parts
                else:
                    self.rgb_preview_viewer.show_image(None)
        
        except Exception as e:
            print(f"Error in preview_rgb_fusion: {e}")
            self.rgb_preview_viewer.show_image(None)
        gc.collect()
    
    def fit_to_screen(self):
        current_tab = self.view_tabs.currentIndex()
        if current_tab == 0:
            self.all_bands_viewer.fit_to_screen()
        elif current_tab == 3:
            self.rgb_preview_viewer.fit_to_screen()
        elif current_tab == 1:
            for i in range(self.individual_bands_notebook.count()):
                viewer = self.individual_bands_notebook.widget(i).findChild(ZoomableImageViewer)
                if viewer:
                    viewer.fit_to_screen()

class ParameterDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Enter Image Parameters")
        layout = QFormLayout()
        self.setLayout(layout)
        
        self.width_entry = QLineEdit("8448")
        layout.addRow("Width:", self.width_entry)
        
        self.height_entry = QLineEdit("384")
        layout.addRow("Height:", self.height_entry)
        
        self.bitdepth_var = QComboBox()
        self.bitdepth_var.addItems(["8", "10", "12"])
        self.bitdepth_var.setCurrentIndex(1)
        layout.addRow("Bit Depth:", self.bitdepth_var)
        
        buttons = QHBoxLayout()
        ok_btn = QPushButton("OK")
        ok_btn.clicked.connect(self.accept)
        buttons.addWidget(ok_btn)
        
        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)
        buttons.addWidget(cancel_btn)
        
        layout.addRow(buttons)
    
    def get_parameters(self):
        return {
            "width": self.width_entry.text(),
            "height": self.height_entry.text(),
            "bit_depth": int(self.bitdepth_var.currentText())
        }

class MainApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("XD Band Display Stable")
        self.resize(1920, 1080)
        
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout()
        central_widget.setLayout(main_layout)
        
        # Add "+" button for new tabs
        button_layout = QHBoxLayout()
        add_tab_btn = QPushButton("+")
        add_tab_btn.setFixedWidth(30)
        add_tab_btn.clicked.connect(self.add_new_tab)
        button_layout.addWidget(add_tab_btn)
        button_layout.addStretch()
        main_layout.addLayout(button_layout)
        
        self.tab_widget = QTabWidget()
        self.tab_widget.setTabBar(CustomTabBar())
        self.tab_widget.tabCloseRequested.connect(self.close_tab)
        main_layout.addWidget(self.tab_widget)
        self.tab_switch_timer = QTimer()
        self.tab_switch_timer.setSingleShot(True)
        self.tab_switch_timer.setInterval(100)  # 100ms debounce
        self.tab_widget.currentChanged.connect(self._debounced_tab_change)

        add_live_btn = QPushButton("Add Live Tab")
        add_live_btn.clicked.connect(self.add_live_tab)
        button_layout.addWidget(add_live_btn)
        
        self.add_new_tab()

    def _debounced_tab_change(self, index):
        self.tab_switch_timer.stop()
        self.tab_switch_timer.timeout.connect(lambda: self._handle_tab_change(index))
        self.tab_switch_timer.start()

    def _handle_tab_change(self, index):
        # Optional: Clear cache of inactive tabs
        for i in range(self.tab_widget.count()):
            if i != index:
                app = self.tab_widget.widget(i)
                if hasattr(app, 'view_cache'):
                    app.view_cache.clear()
        
    def add_new_tab(self):
        app = BandStitchProApp(main_app=self)
        tab_index = self.tab_widget.addTab(app, "New Dataset")
        self.tab_widget.setCurrentIndex(tab_index)
    
    def update_tab_name(self, app, name):
        index = self.tab_widget.indexOf(app)
        if index != -1:
            self.tab_widget.setTabText(index, name)
    
    def close_tab(self, index):
        if self.tab_widget.count() > 1:
            widget = self.tab_widget.widget(index)
            self.tab_widget.removeTab(index)
            widget.deleteLater()
        else:
            QMessageBox.warning(self, "Warning", "Cannot close the last tab")
    
    def add_live_tab(self):
        live_widget = QWidget()
        layout = QVBoxLayout()
        live_widget.setLayout(layout)

        label = QLabel("Live Display")
        layout.addWidget(label)

        # Create VideoModeHandler instance directly
        video_mode = VideoModeHandler(live_widget, filepath=None)
        layout.addWidget(video_mode)

        # Optional: Add a close button
        close_btn = QPushButton("Close Live Display")
        close_btn.clicked.connect(lambda: self.close_tab(self.tab_widget.indexOf(live_widget)))
        layout.addWidget(close_btn)

        tab_index = self.tab_widget.addTab(live_widget, "Live Display")
        self.tab_widget.setCurrentIndex(tab_index)

    # Modify the existing close_tab method in MainApp
    def close_tab(self, index):
        if self.tab_widget.count() > 1:
            widget = self.tab_widget.widget(index)
            if hasattr(widget, 'live_process') and widget.live_process:
                widget.live_process.kill()  # Or send 'e\n' if you prefer graceful shutdown
            self.tab_widget.removeTab(index)
            widget.deleteLater()
        else:
            QMessageBox.warning(self, "Warning", "Cannot close the last tab")

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyleSheet("""
    QWidget {
        background-color: #f0f0f0;
        font: 10pt "Arial";
    }
    QPushButton {
        background-color: #4CAF50;
        color: white;
        border: none;
        padding: 3px 8px;
        border-radius: 3px;
    }
    QPushButton:hover {
        background-color: #45a049;
    }
    QGroupBox {
        font-weight: bold;
        border: 1px solid #ccc;
        border-radius: 5px;
        margin-top: 10px;
    }
    QTabWidget::pane {
        border: 1px solid #ccc;
        background: white;
    }
    """)
    window = MainApp()
    window.show()
    sys.exit(app.exec_())
