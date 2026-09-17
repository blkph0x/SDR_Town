#pragma once

#include <QObject>
#include <QList>
#include <QString>

class QDockWidget;
class QMainWindow;
class QMenu;
class QSettings;
class QWidget;

// Presentation-only ownership. Changing layouts never changes receiver state.
class WorkspaceLayout final : public QObject {
public:
    explicit WorkspaceLayout(QMainWindow* window);
    QDockWidget* addPanel(const QString& id, const QString& title, QWidget* content);
    void populateMenu(QMenu* menu);
    bool applyPreset(const QString& id);
    void setLocked(bool locked);
    bool isLocked() const { return locked_; }
    bool restore(QSettings& settings);
    void save(QSettings& settings) const;
    QString preset() const { return preset_; }

private:
    QMainWindow* window_;
    QList<QDockWidget*> panels_;
    QString preset_ = "listening";
    bool locked_ = false;
};
