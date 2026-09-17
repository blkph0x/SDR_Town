#pragma once
#include <QDialog>
#include <QImage>
#include <functional>
#include <nlohmann/json.hpp>

class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QListWidget;
class QThread;
class QCloseEvent;
class QResizeEvent;

class SstvWindow final : public QDialog {
    Q_OBJECT
public:
    using Decode = std::function<nlohmann::json(const QString&, const QString&, const QString&,
                                               const std::function<bool()>&)>;
    explicit SstvWindow(Decode decode, QWidget* parent = nullptr);
    ~SstvWindow() override;
    bool startDecode(const QString& input, const QString& output, const QString& mode);
    bool busy() const { return worker_ != nullptr; }
    void cancel();
signals:
    void decodeFinished(bool success);
protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void reject() override;
private:
    void setBusy(bool value);
    void updatePreview();
    Decode decode_;
    QThread* worker_ = nullptr;
    bool closePending_ = false;
    QLineEdit *input_, *output_;
    QComboBox* mode_;
    QPushButton *open_, *destination_, *decodeButton_, *cancelButton_, *folder_;
    QLabel *status_, *preview_;
    QListWidget* images_;
    QImage original_;
    QString resultDirectory_;
};
