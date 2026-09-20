#pragma once
#include <QDialog>
#include <QImage>
#include <functional>
#include <atomic>
#include <memory>
#include <nlohmann/json.hpp>
#include "SstvProgress.h"

class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QListWidget;
class QSlider;
class QThread;
class QCloseEvent;
class QResizeEvent;

class SstvWindow final : public QDialog {
    Q_OBJECT
public:
    using Decode = std::function<nlohmann::json(const QString&, const QString&, const QString&,
                                               const std::function<bool()>&, const SstvPreview&)>;
    using LiveOpen = std::function<Decode(const std::shared_ptr<std::atomic<bool>>&)>;
    explicit SstvWindow(Decode decode, QWidget* parent = nullptr);
    ~SstvWindow() override;
    bool startDecode(const QString& input, const QString& output, const QString& mode);
    void setLiveSource(LiveOpen open);
    bool startLive(const QString& output,const QString& mode);
    void finishLive();
    bool busy() const { return worker_ != nullptr; }
    void cancel();
    QString statusMessage() const;
    QString resultDirectory() const { return resultDirectory_; }
    QStringList imagePaths() const;
    QStringList imageLabels() const;
    bool liveSelected() const;
    QString selectedMode() const;
signals:
    void decodeFinished(bool success);
protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void reject() override;
private:
    void setBusy(bool value);
    void updatePreview();
    QImage adjustedPreview() const;
    Decode decode_;
    LiveOpen liveOpen_;
    std::shared_ptr<std::atomic<bool>> finish_;
    QThread* worker_ = nullptr;
    bool closePending_ = false;
    QLineEdit *input_, *output_;
    QComboBox *mode_, *source_;
    QPushButton *open_, *destination_, *decodeButton_, *cancelButton_, *folder_;
    QPushButton* finishButton_;
    QLabel *status_ = nullptr;
    QLabel *preview_ = nullptr;
    QLabel *hint_ = nullptr;
    QListWidget* images_;
    QSlider *brightness_, *contrast_;
    QImage original_;
    int scanline_ = -1;
    QString resultDirectory_;
};
