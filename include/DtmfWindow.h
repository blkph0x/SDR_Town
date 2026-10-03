#pragma once
#include "DtmfDecoder.h"
#include <QDialog>
#include <memory>
#include <thread>
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QTableWidget;

class DtmfWindow final : public QDialog {
    Q_OBJECT
public:
    using Source = std::function<std::shared_ptr<DtmfDecoder>(bool input)>;
    explicit DtmfWindow(Source source, QWidget* parent = nullptr);
    ~DtmfWindow() override;
protected:
    void hideEvent(QHideEvent*) override;
private:
    DtmfOptions selectedOptions() const;
    void refresh();
    void analyze();
    void busy(bool);
    void display(const DtmfSnapshot&);
    struct Work;
    std::shared_ptr<Work> work_;
    std::thread worker_;
    Source live_;
    QComboBox *source_, *profile_, *transform_;
    QDoubleSpinBox *pivot_, *scale_, *shift_;
    QLineEdit* file_;
    QPushButton *open_, *analyze_, *stop_, *apply_;
    QLabel *status_, *details_;
    QTableWidget* history_;
    DtmfSnapshot fileResult_;
};
