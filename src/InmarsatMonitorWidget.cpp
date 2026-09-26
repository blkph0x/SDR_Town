#include "InmarsatMonitorWidget.h"
#include "InmarsatIcaoCountry.h"
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace {
QString hexId(uint32_t id) { return QString("%1").arg(id,6,16,QChar('0')).toUpper(); }
QString clean(QString s) { return s.replace('\t',' ').replace('\n',' ').replace('\r',' '); }
class MonitorItem final : public QTableWidgetItem {
public:
    bool operator<(const QTableWidgetItem& other) const override {
        const auto a=data(Qt::UserRole+1), b=other.data(Qt::UserRole+1);
        if(a.isValid() && b.isValid())return a.toDouble()<b.toDouble();
        return QTableWidgetItem::operator<(other);
    }
};
void cell(QTableWidget* table, int row, int column, const QVariant& value) {
    auto* item = new MonitorItem;
    // Store display text as text: QStyledItemDelegate otherwise rounds doubles
    // again through QLocale, even when item->text() looks precise in a test.
    item->setData(Qt::DisplayRole, value.metaType().id()==QMetaType::Double
        ? QString::number(value.toDouble(),'g',12) : value.toString());
    if(value.metaType().id()!=QMetaType::QString)item->setData(Qt::UserRole+1,value);
    table->setItem(row,column,item);
}
}

InmarsatMonitorWidget::InmarsatMonitorWidget(View view, QWidget* parent, bool popout)
    : QWidget(parent), view_(view) {
    auto* root = new QVBoxLayout(this);
    auto* tools = new QHBoxLayout;
    count_ = new QLabel; tools->addWidget(count_);
    const auto button = [&](const char* name, const char* tip, QStyle::StandardPixmap icon) {
        auto* b = new QToolButton; b->setObjectName(name); b->setToolTip(tip);
        b->setAccessibleName(tip); b->setIcon(style()->standardIcon(icon)); tools->addWidget(b); return b;
    };
    auto* copy = button("inmarsatMonitorCopy","Copy visible rows",QStyle::SP_DialogSaveButton);
    copy->setIcon(QIcon{});copy->setText(QString(QChar(0x2398)));
    auto* clear = button("inmarsatMonitorClear",view==View::Aircraft ? "Clear aircraft and map positions" : "Clear status history",QStyle::SP_TrashIcon);
    if (view==View::Aircraft) {
        positions_ = new QCheckBox("With position only"); positions_->setObjectName("inmarsatAircraftPositionOnly");
        tools->addWidget(positions_);
        connect(positions_,&QCheckBox::toggled,this,[this]{refresh();});
    }
    tools->addStretch();
    if(popout) {
        auto* open=button("inmarsatMonitorPopout","Open in separate window",QStyle::SP_TitleBarMaxButton);
        connect(open,&QToolButton::clicked,this,[this] {
            auto* dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setWindowTitle(view_==View::Aircraft ? "Inmarsat aircraft" : "Inmarsat decoders");
            auto* layout=new QVBoxLayout(dialog);
            layout->addWidget(new InmarsatMonitorWidget(view_,dialog,false));
            dialog->resize(1000,500);dialog->show();
        });
    }
    root->addLayout(tools);
    table_=new QTableWidget(0,view==View::Aircraft ? 12 : 8);
    table_->setObjectName(view==View::Aircraft ? "inmarsatAircraftTable" : "inmarsatDecoderTable");
    if(view==View::Aircraft)
        table_->setHorizontalHeaderLabels({"AES","ICAO","Country","Reg","Flight","Lat","Lon","Alt (ft)","Age (s)","Msgs","Position age (s)","MHz"});
    else table_->setHorizontalHeaderLabels({"Channel","MHz","bit/s","Status","Msgs","CRC OK","CRC bad","IQ gaps"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSortingEnabled(true);table_->sortItems(0);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->horizontalHeader()->setDefaultSectionSize(100);
    table_->horizontalHeader()->setStretchLastSection(true);
    if(view==View::Aircraft) {
        table_->setColumnWidth(2,160);
        table_->horizontalHeaderItem(1)->setToolTip("Explicit ADS-C airframe ID; unknown stays blank");
        table_->horizontalHeaderItem(2)->setToolTip("ICAO address allocation, not aircraft location");
        table_->horizontalHeaderItem(8)->setToolTip("Seconds since last validated message received");
        table_->horizontalHeaderItem(9)->setToolTip("Validated decoded messages observed, not unique transmissions");
    }
    root->addWidget(table_,1);
    if(view==View::Decoders) {
        log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(500);
        log_->setObjectName("inmarsatDecoderHistory");root->addWidget(log_,1);
    }
    connect(copy,&QToolButton::clicked,this,[this]{QApplication::clipboard()->setText(tableText());});
    connect(clear,&QToolButton::clicked,this,[this]{
        if(view_==View::Aircraft)InmarsatMessageStore::instance().clearAircraft();
        else log_->clear();
        refresh();
    });
    timer_=new QTimer(this);
    connect(timer_,&QTimer::timeout,this,[this]{refresh();});
}

void InmarsatMonitorWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);refresh();timer_->start(500); // DEC-0138: existing GUI budget.
}
void InmarsatMonitorWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);timer_->stop();
}
void InmarsatMonitorWidget::refresh() {
    updateSnapshot(InmarsatEngine::instance().snapshot(),InmarsatMessageStore::instance().aircraft(),
                   QDateTime::currentMSecsSinceEpoch()/1000.0);
}
QString InmarsatMonitorWidget::tableText() const {
    QStringList lines, headers;
    for(int c=0;c<table_->columnCount();++c)headers<<table_->horizontalHeaderItem(c)->text();
    lines<<headers.join('\t');
    for(int r=0;r<table_->rowCount();++r) {
        QStringList cells;
        for(int c=0;c<table_->columnCount();++c)
            cells<<clean(table_->item(r,c)?table_->item(r,c)->text():QString{});
        lines<<cells.join('\t');
    }
    return lines.join('\n');
}

void InmarsatMonitorWidget::updateSnapshot(const InmarsatEngineSnapshot& snapshot,
    const std::vector<InmarsatAircraft>& aircraft,double now) {
    const auto selected=table_->currentRow()>=0 && table_->item(table_->currentRow(),0)
        ? table_->item(table_->currentRow(),0)->data(Qt::UserRole).toString():QString{};
    const int scroll=table_->verticalScrollBar()->value();
    table_->setUpdatesEnabled(false);table_->setSortingEnabled(false);table_->setRowCount(0);
    if(view_==View::Aircraft) {
        for(const auto& a:aircraft) {
            const auto& m=a.identity;
            if(positions_->isChecked()&&!m.hasPosition)continue;
            const int r=table_->rowCount();table_->insertRow(r);
            cell(table_,r,0,hexId(m.aesId));table_->item(r,0)->setData(Qt::UserRole,hexId(m.aesId));
            bool valid=false;const auto icao=QString::fromStdString(m.icaoHex);
            const auto number=icao.toUInt(&valid,16);
            cell(table_,r,1,icao);
            cell(table_,r,2,valid && icao.size()==6 ? QString::fromUtf8(inmarsatIcaoCountry(number).data(),int(inmarsatIcaoCountry(number).size())) : QString{});
            cell(table_,r,3,QString::fromStdString(m.registration));cell(table_,r,4,QString::fromStdString(m.callsign));
            if(m.hasPosition) {
                cell(table_,r,5,m.latDeg);cell(table_,r,6,m.lonDeg);
                if(std::isfinite(m.altitudeFt))cell(table_,r,7,m.altitudeFt);
                cell(table_,r,10,qulonglong(std::max(0.0,now-a.positionTime)));
            }
            cell(table_,r,8,qulonglong(std::max(0.0,now-m.unixTime)));
            cell(table_,r,9,qulonglong(a.messages));cell(table_,r,11,m.freqHz/1e6);
        }
        count_->setText(QString("%1 tracked | %2 shown").arg(aircraft.size()).arg(table_->rowCount()));
    } else {
        const bool running=snapshot.state!=InmarsatEngineState::Idle;
        QMap<QString,QString> current;
        const auto add=[&](const QString& key,const QString& label,double freq,int rate,const nlohmann::json& d,bool active,bool enabled) {
            const QString state=!running ? "Stopped" : !enabled ? "Disabled" : !active ? "Not active" :
                d.value("protocolLock",false) ? "Locked" : d.value("carrierDetected",false) ? "Acquiring" : "Searching";
            const int r=table_->rowCount();table_->insertRow(r);
            cell(table_,r,0,label);table_->item(r,0)->setData(Qt::UserRole,key);
            cell(table_,r,1,freq/1e6);cell(table_,r,2,rate);cell(table_,r,3,state);
            if(active) {
                const char* fields[]={"messages","validatedFrames","crcFailed","discontinuities"};
                for(int c=0;c<4;++c)cell(table_,r,c+4,qulonglong(d.value(fields[c],uint64_t{0})));
            }
            const auto signature=QString("%1 MHz | %2 bit/s | %3").arg(freq/1e6,0,'f',6).arg(rate).arg(state);
            current[key]=signature;
            if(states_.value(key)!=signature)
                log_->appendPlainText(QDateTime::fromMSecsSinceEpoch(qint64(now*1000)).toString(Qt::ISODate)+" | "+label+" | "+signature);
        };
        if(snapshot.config.watch.enabled) {
            const auto watch=snapshot.diagnostics.value("watch",nlohmann::json::object());
            const auto channels=watch.value("channels",nlohmann::json::array());
            for(const auto& ch:snapshot.config.watch.channels) {
                nlohmann::json d=nlohmann::json::object();bool active=false;
                for(const auto& item:channels)if(item.value("id",std::string{})==ch.id) {
                    d=item.value("decoder",nlohmann::json::object());active=true;break;
                }
                add(QString::fromStdString(ch.id),QString::fromStdString(ch.label.empty()?ch.id:ch.label),ch.frequencyHz,std::abs(ch.rate),d,active,ch.enabled);
            }
        } else add("manual","Manual",snapshot.config.channelHz,snapshot.config.baud,snapshot.diagnostics,true,true);
        for(auto it=states_.cbegin();it!=states_.cend();++it)
            if(!current.contains(it.key()))log_->appendPlainText(QDateTime::fromMSecsSinceEpoch(qint64(now*1000)).toString(Qt::ISODate)+" | "+it.key()+" | Removed");
        states_=current;
        count_->setText(QString("%1 channels | %2").arg(table_->rowCount()).arg(running?"Running":"Stopped"));
    }
    table_->setSortingEnabled(true);
    for(int r=0;r<table_->rowCount();++r)
        if(!selected.isEmpty() && table_->item(r,0)->data(Qt::UserRole).toString()==selected)table_->selectRow(r);
    table_->verticalScrollBar()->setValue(scroll);table_->setUpdatesEnabled(true);
}
