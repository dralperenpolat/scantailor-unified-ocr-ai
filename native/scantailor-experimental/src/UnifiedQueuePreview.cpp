// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnifiedRuntime.h"
#include "UnifiedQueuePreview.h"
#include "BasicImageView.h"
#include "acceleration/NonAcceleratedOperations.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QThread>
#include <QVBoxLayout>
#include <limits>

UnifiedQueuePreview::UnifiedQueuePreview(QWidget* parent) : QWidget(parent)
{
    setObjectName("unifiedQueuePreview");
    auto layout = new QVBoxLayout(this);
    auto bar = new QHBoxLayout;
    layout->addLayout(bar);
    m_title = new QLabel;
    m_title->setObjectName("queuePreviewTitle");
    m_title->setWordWrap(true);
    m_title->setMinimumWidth(80);
    bar->addWidget(m_title, 1);
    for(auto spec:QList<QPair<QString,int>>{{QString::fromUtf8("Bu sayfayı düzenle"),3},{QString::fromUtf8("Geometri"),2},{QString::fromUtf8("Kenarlar"),4},{QString::fromUtf8("Parmak sil"),6}}){
        auto edit=new QPushButton(spec.first);edit->setObjectName(QString("queueEditStage%1").arg(spec.second));bar->addWidget(edit);
        connect(edit,&QPushButton::clicked,this,[this,spec]{if(editRequested)editRequested(spec.second);});
    }
    auto back = new QPushButton(QString::fromUtf8("Açık projeye dön"));
    bar->addWidget(back);
    connect(back, &QPushButton::clicked, this, [this] { if (returnToEditor) returnToEditor(); });
    auto navigation = new QHBoxLayout;
    layout->addLayout(navigation);
    m_previous = new QPushButton(QString::fromUtf8("‹"));
    m_next = new QPushButton(QString::fromUtf8("›"));
    m_previous->setMaximumWidth(35); m_next->setMaximumWidth(35);
    m_previous->setToolTip(QString::fromUtf8("Önceki görüntü")); m_next->setToolTip(QString::fromUtf8("Sonraki görüntü"));
    m_choices = new QComboBox;
    m_choices->setObjectName("queuePreviewPages");
    m_choices->setMinimumWidth(80);
    m_choices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    navigation->addWidget(m_previous); navigation->addWidget(m_choices, 1); navigation->addWidget(m_next);
    m_quality=new QCheckBox(QString::fromUtf8("Net önizleme · 300 DPI"));m_quality->setObjectName("queuePreviewQuality");m_quality->setChecked(true);
    m_quality->setToolTip(QString::fromUtf8("Seçili PDF sayfasını 300 DPI ile yükler. Kaynak fotoğrafta asıl çözünürlüğü kullanır. PDF dosyası değişmez."));navigation->addWidget(m_quality);
    connect(m_quality,&QCheckBox::toggled,this,[this]{selectImage();});
    for (auto spec : QList<QPair<QString, double>>{{"+", 1.25}, {QString::fromUtf8("−"), .8}, {QString::fromUtf8("Sığdır"), 0}}) {
        auto button = new QPushButton(spec.first); navigation->addWidget(button);
        if (spec.second) button->setFixedWidth(30);
        connect(button, &QPushButton::clicked, this, [this, spec] {
            if (!m_view) return;
            m_view->setZoomLevel(spec.second ? qBound(1., m_view->zoomLevel() * spec.second, 30.) : 1.);
            if (!spec.second) m_view->moveTowardsIdealPosition(std::numeric_limits<double>::max());
        });
    }
    m_message = new QLabel;
    m_message->setObjectName("queuePreviewMessage");
    m_message->setWordWrap(true);
    layout->addWidget(m_message);
    m_imageLayout = new QVBoxLayout;
    m_imageLayout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(m_imageLayout, 1);
    connect(m_choices, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { selectImage(); });
    connect(m_previous, &QPushButton::clicked, this, [this] { m_choices->setCurrentIndex(m_choices->currentIndex() - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] { m_choices->setCurrentIndex(m_choices->currentIndex() + 1); });
}

UnifiedQueuePreview::~UnifiedQueuePreview()
{
    if (m_loader) { m_loader->requestInterruption(); m_loader->wait(); delete m_loader; }
}

void UnifiedQueuePreview::showSelection(QString const& title, QJsonArray const& images, QString const& path)
{
    m_title->setText(QString::fromUtf8("Kuyruk seçimi · ") + title);
    QSignalBlocker block(m_choices);
    m_choices->clear();
    int selected = path.isEmpty() || path.endsWith(".pdf", Qt::CaseInsensitive) ? 0 : -1;
    for (auto value : images) {
        auto image = value.toObject();
        if (image.value("path").toString() == path) selected = m_choices->count();
        m_choices->addItem(image.value("label").toString(), image.value("path").toString());
        m_choices->setItemData(m_choices->count() - 1, image.value("revision").toString(), Qt::UserRole + 1);
        m_choices->setItemData(m_choices->count() - 1, image.value("pdf").toString(), Qt::UserRole + 2);
        m_choices->setItemData(m_choices->count() - 1, image.value("pdf_page").toInt(), Qt::UserRole + 3);
    }
    if (selected < 0) {
        // A requested file may disappear or be rewritten after the event.
        // Show its read error instead of silently substituting another page.
        selected = m_choices->count();
        m_choices->addItem(QString::fromUtf8("Seçilen sonuç · dosya kontrol ediliyor"), path);
    }
    m_choices->setCurrentIndex(m_choices->count() ? selected : -1);
    if (currentPath() != m_pendingPath || m_choices->currentData(Qt::UserRole + 1).toString() != m_revision || !m_view)
        selectImage();
    else if (currentPathChanged) currentPathChanged(currentPath());
}

QString UnifiedQueuePreview::currentPath() const { return m_choices->currentData().toString(); }
void UnifiedQueuePreview::setCurrentPath(QString const& path)
{
    int index = m_choices->findData(path);
    if (index >= 0) m_choices->setCurrentIndex(index);
}

void UnifiedQueuePreview::selectImage()
{
    ++m_generation;
    if(m_loader)m_loader->requestInterruption();
    m_pendingPath = m_choices->currentData().toString();
    m_revision = m_choices->currentData(Qt::UserRole + 1).toString();
    m_choices->setToolTip(m_pendingPath);
    m_previous->setEnabled(m_choices->currentIndex() > 0);
    m_next->setEnabled(m_choices->currentIndex() >= 0 && m_choices->currentIndex() + 1 < m_choices->count());
    delete m_view.data();
    m_message->setText(m_pendingPath.isEmpty()
        ? QString::fromUtf8("Bu kitap için okunabilir bir kaynak veya tamamlanmış önizleme henüz bulunamadı.")
        : QString::fromUtf8("Görüntü yükleniyor…"));
    loadImage();
    if (currentPathChanged) currentPathChanged(m_pendingPath);
}

void UnifiedQueuePreview::loadImage(bool fullResolution)
{
    if (m_loader || m_pendingPath.isEmpty()) return;
    QString path = m_pendingPath;
    quint64 generation = m_generation;
    auto result = std::make_shared<QImage>();
    QString pdf=m_choices->currentData(Qt::UserRole+2).toString();int pdfPage=m_choices->currentData(Qt::UserRole+3).toInt();
    QString root=qEnvironmentVariable("STOU_ROOT");
    // Decode at most one image at a time. Rapid selection changes discard old
    // results, so a slow previous book cannot replace the latest selection.
    m_loader = QThread::create([path, result, fullResolution,pdf,pdfPage,root] {
        QTemporaryDir temp;
        QString input=path;
        if(fullResolution&&!pdf.isEmpty()){
            QString python=unified_runtime::python(root),script=root+"/src/scantailor_ocr_unified/preview_render.py";
            if(!QFileInfo::exists(python)||!QFileInfo::exists(script)||!temp.isValid())return;
            input=temp.filePath("page.png");QProcess render;
            render.start(python,{script,pdf,QString::number(pdfPage),input});
            QElapsedTimer clock;clock.start();
            while(!render.waitForFinished(100)){
                if(QThread::currentThread()->isInterruptionRequested()||clock.elapsed()>30000){render.kill();render.waitForFinished(1000);return;}
                if(render.state()==QProcess::NotRunning)return;
            }
            if(render.exitStatus()!=QProcess::NormalExit||render.exitCode()!=0)return;
        }
        QImageReader reader(input);
        reader.setAutoTransform(true);
        auto size = reader.size();
        int limit=fullResolution?12000:2400;
        if(size.isValid()&&fullResolution&&double(size.width())*size.height()>50000000.)limit=qMin(limit,int(qMax(size.width(),size.height())*std::sqrt(50000000./(double(size.width())*size.height()))));
        if (size.isValid() && (size.width() > limit || size.height() > limit))
            reader.setScaledSize(size.scaled(limit, limit, Qt::KeepAspectRatio));
        *result = reader.read();
        if (result->width() > limit || result->height() > limit)
            *result = result->scaled(limit, limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    });
    connect(m_loader, &QThread::finished, this, [this, generation, result, fullResolution,pdf] {
        m_loader->deleteLater(); m_loader = nullptr;
        if (generation != m_generation) { loadImage(); return; }
        if (result->isNull()) {
            m_message->setText(fullResolution?QString::fromUtf8("Net önizleme yüklenemedi; hızlı önizleme gösteriliyor. Satırı yeniden seçerek deneyebilirsiniz."):QString::fromUtf8("Görüntü okunamadı. Henüz yazılıyor olabilir; satırı yeniden seçebilirsiniz."));
            return;
        }
        double zoom=m_view?m_view->zoomLevel():1.;QPointF center(.5,.5);
        if(m_view){auto r=m_view->virtualDisplayRect();auto p=m_view->widgetToVirtual().map(m_view->viewport()->rect().center());center=QPointF((p.x()-r.x())/r.width(),(p.y()-r.y())/r.height());delete m_view.data();}
        m_view = new BasicImageView(std::make_shared<NonAcceleratedOperations>(), *result);
        m_view->setProperty("previewPath", m_pendingPath);
        m_view->setProperty("previewPixels",result->size());m_view->setProperty("fullResolution",fullResolution);
        m_view->setReadOnlyNavigation(true);
        m_imageLayout->addWidget(m_view);
        m_imageLayout->activate();m_view->setZoomLevel(zoom);
        auto r=m_view->virtualDisplayRect();auto p=m_view->virtualToWidget().map(QPointF(r.x()+center.x()*r.width(),r.y()+center.y()*r.height()));
        m_view->setWidgetFocalPoint(m_view->getWidgetFocalPoint()+QPointF(m_view->viewport()->rect().center())-p);
        m_message->setText(fullResolution?(pdf.isEmpty()?QString::fromUtf8("Kaynak / çıktı görüntüsü · net önizleme"):QString::fromUtf8("PDF'nin 300 DPI görünümü"))+QString(" · %1 × %2 px").arg(result->width()).arg(result->height()):QString::fromUtf8("Hızlı önizleme · net görüntü yükleniyor…"));
        if(!fullResolution&&m_quality->isChecked())loadImage(true);
        else if(!fullResolution)m_message->setText(QString::fromUtf8("Hızlı önizleme · net görünüm için 300 DPI seçeneğini açın."));
    });
    m_loader->start();
}
