// App-wide dark theme approximating the macOS SwiftUI look: layered panels,
// rounded cards, an accent colour, and consistently styled controls. Applied on
// top of the Fusion style in main().
#pragma once

namespace lumenwin {

inline const char* kAppStyle = R"QSS(
* { font-family: "Segoe UI", "Inter", sans-serif; }
QWidget { background: #1b1d23; color: #e7e9ef; font-size: 13px; }
QMainWindow { background: #141519; }
QLabel, QCheckBox, QRadioButton, QGroupBox { background: transparent; }

QMenuBar { background: #141519; color: #c9cdd6; }
QMenuBar::item { padding: 5px 10px; background: transparent; }
QMenuBar::item:selected { background: #2a2e38; border-radius: 5px; }
QMenu { background: #23262e; border: 1px solid #333844; padding: 4px; }
QMenu::item { padding: 5px 22px; border-radius: 5px; }
QMenu::item:selected { background: #4f7cf0; }

QStatusBar { background: #141519; color: #9aa2b1; }
QToolTip { background: #23262e; color: #e7e9ef; border: 1px solid #3a3f4c;
           border-radius: 6px; padding: 4px 6px; }

QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #363b47; border-radius: 5px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: #454b59; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QGroupBox { border: 1px solid #2f3440; border-radius: 12px; margin-top: 16px;
            padding: 12px 12px 12px 12px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 14px; top: 3px;
                   padding: 0 6px; color: #98a0b0; font-weight: 600; }

QPushButton { background: #2c303b; color: #e7e9ef; border: 1px solid #3a3f4c;
              border-radius: 8px; padding: 7px 12px; }
QPushButton:hover { background: #363b47; border-color: #454b59; }
QPushButton:pressed { background: #414857; }
QPushButton:disabled { color: #676c7b; background: #23252c; border-color: #2b2e37; }
QPushButton#accent { background: #4f7cf0; color: #ffffff; border: none; font-weight: 600; }
QPushButton#accent:hover { background: #5f8af5; }
QPushButton#accent:pressed { background: #4470e0; }
QPushButton#accent:disabled { background: #313a52; color: #8593b4; }

QComboBox { background: #2a2d36; border: 1px solid #3a3f4c; border-radius: 7px;
            padding: 5px 10px; }
QComboBox:hover { border-color: #4a5060; }
QComboBox::drop-down { border: none; width: 20px; }
QComboBox QAbstractItemView { background: #23262e; border: 1px solid #333844;
                              border-radius: 8px; selection-background-color: #4f7cf0;
                              outline: none; padding: 4px; }

QLineEdit { background: #22252d; border: 1px solid #3a3f4c; border-radius: 7px;
            padding: 4px 8px; }
QLineEdit:focus { border-color: #4f7cf0; }

QSpinBox, QDoubleSpinBox { background: #22252d; border: 1px solid #3a3f4c;
                          border-radius: 7px; padding: 3px 6px; }
QSpinBox:focus, QDoubleSpinBox:focus { border-color: #4f7cf0; }

QSlider::groove:horizontal { height: 5px; background: #343945; border-radius: 3px; }
QSlider::sub-page:horizontal { background: #4f7cf0; border-radius: 3px; }
QSlider::add-page:horizontal { background: #343945; border-radius: 3px; }
QSlider::handle:horizontal { background: #f0f2f6; width: 15px; height: 15px;
                             margin: -6px 0; border-radius: 8px; border: 1px solid #b6bac4; }
QSlider::handle:horizontal:hover { background: #ffffff; }

QCheckBox::indicator, QRadioButton::indicator { width: 17px; height: 17px;
    border: 1px solid #4a5060; background: #22252d; }
QCheckBox::indicator { border-radius: 5px; }
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background: #4f7cf0; border-color: #4f7cf0; }
)QSS";

}  // namespace lumenwin
