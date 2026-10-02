#include "WorkspaceLayout.h"

#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QScrollArea>
#include <QSettings>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QLabel>
#include <QSignalBlocker>

namespace {
constexpr int layoutVersion = 1;
const QString settingsRoot = QStringLiteral("workspace/v1/");

// Large tables/tool rows must scroll rather than forcing the whole window
// wider than a laptop display. The contained widgets keep their own minima.
class PanelScrollArea final : public QScrollArea {
public:
    using QScrollArea::QScrollArea;
    QSize sizeHint() const override { return {800, 260}; }
    QSize minimumSizeHint() const override { return {240, 120}; }
};
}

WorkspaceLayout::WorkspaceLayout(QMainWindow* window) : QObject(window), window_(window) {
    window_->setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    window_->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
    auto* toolbar = window_->addToolBar("Workspace");
    toolbar->setObjectName("workspace.navigation");
    toolbar->setMovable(false);
    toolbar->addWidget(new QLabel(" Workspace: ", toolbar));
    presetSelector_ = new QComboBox(toolbar);
    presetSelector_->setObjectName("workspace.selector");
    presetSelector_->setAccessibleName("Workspace");
    for (const auto& choice : QList<QPair<QString, QString>>{
             {"Listen", "listening"}, {"P25 Trunking", "trunking"},
             {"HF / DX", "hf"}, {"Satellites / Aircraft", "satellite"},
             {"Signal Analysis", "analysis"}})
        presetSelector_->addItem(choice.first, choice.second);
    toolbar->addWidget(presetSelector_);
    connect(presetSelector_, &QComboBox::activated, this, [this](int index) {
        applyPreset(presetSelector_->itemData(index).toString());
    });
    auto* panelsButton = new QToolButton(toolbar);
    panelsButton->setText("Panels");
    panelsButton->setToolTip("Show or hide workspace panels");
    panelsButton->setPopupMode(QToolButton::InstantPopup);
    auto* panelMenu = new QMenu(panelsButton);
    panelsButton->setMenu(panelMenu);
    connect(panelMenu, &QMenu::aboutToShow, this, [this, panelMenu] {
        panelMenu->clear();
        for (auto* dock : panels_) panelMenu->addAction(dock->toggleViewAction());
    });
    toolbar->addWidget(panelsButton);
}

QDockWidget* WorkspaceLayout::addPanel(const QString& id, const QString& title, QWidget* content) {
    auto* dock = new QDockWidget(title, window_);
    dock->setObjectName("workspace." + id);
    auto* scroll = new PanelScrollArea(dock);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    dock->setWidget(scroll);
    window_->addDockWidget(Qt::BottomDockWidgetArea, dock);
    panels_.push_back(dock);
    setLocked(locked_);
    return dock;
}

bool WorkspaceLayout::applyPreset(const QString& id) {
    QString selected;
    if (id == "listening" || id == "hf") selected = "saved";
    else if (id == "trunking") selected = "p25";
    else if (id == "analysis") selected = "capture";
    else if (id == "satellite") selected = "satcom";
    else return false;
    preset_ = id;
    const QSignalBlocker selectorBlock(presetSelector_);
    presetSelector_->setCurrentIndex(presetSelector_->findData(id));
    QDockWidget* previous = nullptr;
    QDockWidget* active = nullptr;
    for (auto* dock : panels_) {
        dock->setFloating(false);
        window_->removeDockWidget(dock);
        window_->addDockWidget(Qt::BottomDockWidgetArea, dock);
        const auto name = dock->objectName();
        bool visible = name != "workspace.tx"; // Experimental TX is opt-in.
        if (id == "listening" && name == "workspace.p25") visible = false;
        if (id == "listening" && name == "workspace.satcom") visible = false;
        if (id == "hf" && (name == "workspace.p25" || name == "workspace.receivers")) visible = false;
        if (name == "workspace.repeater") visible = false;
        if (id == "satellite") visible = name == "workspace.satcom";
        dock->setVisible(visible);
        if (visible) {
            if (previous) window_->tabifyDockWidget(previous, dock);
            previous = dock;
        }
        if (dock->objectName() == "workspace." + selected) active = dock;
    }
    if (active) {
        active->show();
        active->raise();
        window_->resizeDocks({active}, {id == "satellite" ? 480 : id == "trunking" ? 320 : 220}, Qt::Vertical);
    }
    return true;
}

void WorkspaceLayout::setLocked(bool locked) {
    locked_ = locked;
    for (auto* dock : panels_) {
        auto features = QDockWidget::DockWidgetFeatures(QDockWidget::DockWidgetClosable);
        if (!locked) features |= QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable;
        dock->setFeatures(features);
    }
}

void WorkspaceLayout::save(QSettings& settings) const {
    settings.setValue(settingsRoot + "geometry", window_->saveGeometry());
    settings.setValue(settingsRoot + "state", window_->saveState(layoutVersion));
    settings.setValue(settingsRoot + "preset", preset_);
    settings.setValue(settingsRoot + "locked", locked_);
}

bool WorkspaceLayout::restore(QSettings& settings) {
    if (!applyPreset(settings.value(settingsRoot + "preset", "listening").toString()))
        applyPreset("listening");
    const auto state = settings.value(settingsRoot + "state").toByteArray();
    if (state.isEmpty()) return false;
    const bool restored = window_->restoreState(state, layoutVersion);
    if (!restored) {
        setLocked(false);
        applyPreset("listening");
        return false;
    }
    const auto geometry = settings.value(settingsRoot + "geometry").toByteArray();
    if (!geometry.isEmpty()) window_->restoreGeometry(geometry);
    setLocked(settings.value(settingsRoot + "locked", false).toBool());
    // Listening must keep Satcom/Inmarsat/Aircraft dark — restoreState can re-show
    // the dock and wake timers that fight the WFM demod path.
    if (preset_ == "listening" || preset_ == "hf") {
        for (auto* dock : panels_) {
            if (dock && dock->objectName() == "workspace.satcom")
                dock->setVisible(false);
        }
    }
    return true;
}

void WorkspaceLayout::populateMenu(QMenu* menu) {
    auto* presets = menu->addMenu("Workspace");
    const QList<QPair<QString, QString>> choices = {
        {"Listening", "listening"}, {"Trunking", "trunking"},
        {"HF / DX", "hf"}, {"Satellites / Aircraft", "satellite"}, {"Analysis", "analysis"}};
    for (const auto& choice : choices) {
        auto* action = presets->addAction(choice.first);
        connect(action, &QAction::triggered, this, [this, id = choice.second] { applyPreset(id); });
    }
    menu->addSeparator();
    for (auto* dock : panels_) menu->addAction(dock->toggleViewAction());
    menu->addSeparator();
    auto* lock = menu->addAction("Lock Panel Positions");
    lock->setCheckable(true);
    connect(lock, &QAction::toggled, this, &WorkspaceLayout::setLocked);
    connect(menu, &QMenu::aboutToShow, this, [this, lock] { lock->setChecked(locked_); });
    menu->addAction("Save Layout", this, [this] { QSettings settings; save(settings); });
    menu->addAction("Restore Saved Layout", this, [this] { QSettings settings; restore(settings); });
    menu->addAction("Reset Layout", this, [this] { setLocked(false); applyPreset("listening"); });
}
