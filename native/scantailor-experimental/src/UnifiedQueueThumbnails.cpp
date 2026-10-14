// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnifiedI18n.h"
#include "UnifiedQueueThumbnails.h"
#include <QComboBox>
#include <QEvent>
#include <QImageReader>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QThread>
#include <QTimer>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QMap>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

namespace {
enum { Path = Qt::UserRole, Kind, Natural, CacheKey, Width, Height };
QString keyFor(QJsonObject const& image) { return image.value("path").toString() + "|" + image.value("revision").toString(); }

class ThumbnailDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(QStyleOptionViewItem const&, QModelIndex const&) const override { return QSize(180, 248); }
    void paint(QPainter* painter, QStyleOptionViewItem const& option, QModelIndex const& index) const override {
        painter->save();
        bool selected = option.state & QStyle::State_Selected;
        QRect area = option.rect.adjusted(4, 4, -4, -4);
        painter->fillRect(area, option.palette.brush(selected ? QPalette::Highlight : QPalette::Base));
        painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        painter->setFont(option.font);
        QRect picture = area.adjusted(6, 4, -6, -34);
        auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (icon.isNull()) painter->drawText(picture, Qt::AlignCenter, unified_i18n::text("Yükleniyor…"));
        else {
            auto pixmap = icon.pixmap(picture.size().boundedTo(QSize(220, 210)));
            QSize size = pixmap.size() / pixmap.devicePixelRatio();
            QPoint position(picture.center().x() - size.width() / 2, picture.center().y() - size.height() / 2);
            painter->drawPixmap(position, pixmap);
        }
        QFontMetrics metrics(option.font);
        QString label = metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, area.width() - 8);
        painter->drawText(QPoint(area.center().x() - metrics.horizontalAdvance(label) / 2, area.bottom() - 10), label);
        painter->restore();
    }
};
}

UnifiedQueueThumbnails::UnifiedQueueThumbnails(QWidget* parent) : QWidget(parent)
{
    setObjectName("unifiedQueueThumbnails");
    auto layout = new QVBoxLayout(this); layout->setContentsMargins(4, 4, 4, 4);
    m_filter = new QComboBox; m_filter->setObjectName("queueThumbnailFilter");
    m_filter->addItem(unified_i18n::text("İşlenmiş sayfalar"), "processed");
    m_filter->addItem(unified_i18n::text("Kaynak fotoğraflar"), "source");
    m_filter->addItem(unified_i18n::text("Tümü"), "all");
    layout->addWidget(m_filter);
    m_note = new QLabel; m_note->setWordWrap(true); layout->addWidget(m_note);
    m_list = new QListWidget; m_list->setObjectName("queueThumbnailList");
    m_list->setViewMode(QListView::ListMode); m_list->setFlow(QListView::TopToBottom);
    m_list->setWrapping(false); m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static); m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setIconSize(QSize(220, 210)); m_list->setItemDelegate(new ThumbnailDelegate(m_list));
    m_list->setWordWrap(true); m_list->setUniformItemSizes(true);
    layout->addWidget(m_list, 1);
    m_order = new QComboBox; m_order->setObjectName("queueThumbnailOrder");
    m_order->addItems({unified_i18n::text("Kitap sırası"), unified_i18n::text("Genişliğe göre"), unified_i18n::text("Yüksekliğe göre"), unified_i18n::text("En / boy oranına göre")});
    layout->addWidget(m_order);
    m_timer = new QTimer(this); m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] { loadVisible(); });
    m_list->viewport()->installEventFilter(this);
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { m_timer->start(20); });
    connect(m_filter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { applyFilter(); });
    connect(m_order, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { loadDimensions(); applyOrder(); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item, QListWidgetItem*) {
        if (item && selectionChanged) selectionChanged(item->data(Path).toString());
    });
}

UnifiedQueueThumbnails::~UnifiedQueueThumbnails()
{
    for (auto thread : {m_loader, m_metadata}) if (thread) { thread->requestInterruption(); thread->wait(); delete thread; }
}

void UnifiedQueueThumbnails::setImages(QJsonArray const& images, QString const& selectedPath)
{
    if (images == m_images) { setCurrentPath(selectedPath); return; }
    ++m_generation; m_images = images; m_failed.clear();
    if (m_metadata) m_metadata->requestInterruption();
    int scroll = m_list->verticalScrollBar()->value();
    QSignalBlocker block(m_list); m_list->clear();
    int processed = 0, sources = 0, natural = 0;
    for (auto value : images) {
        auto image = value.toObject(); QString kind = image.value("kind").toString("source");
        if (kind == "processed") ++processed; else ++sources;
        auto item = new QListWidgetItem(image.value("label").toString(), m_list);
        item->setTextAlignment(Qt::AlignHCenter); item->setToolTip(image.value("path").toString());
        item->setData(Path, image.value("path").toString()); item->setData(Kind, kind);
        item->setData(Natural, natural++); item->setData(CacheKey, keyFor(image));
        if (auto size = m_sizes.object(keyFor(image))) { item->setData(Width, size->width()); item->setData(Height, size->height()); }
    }
    m_filter->setItemText(0, unified_i18n::text("İşlenmiş sayfalar (%1)").arg(processed));
    m_filter->setItemText(1, unified_i18n::text("Kaynak fotoğraflar (%1)").arg(sources));
    m_note->setText(processed ? unified_i18n::text("Seçilen kitabın çıktıları · tıkla: üstte aç")
        : unified_i18n::text("Henüz işlenmiş çıktı yok. Kaynak fotoğraflar gösterilebilir; çıktı oluşunca liste yenilenir."));
    applyFilter(); applyOrder(); setCurrentPath(selectedPath);
    m_list->verticalScrollBar()->setValue(scroll);
    if (m_list->currentItem()) m_list->scrollToItem(m_list->currentItem(), QAbstractItemView::EnsureVisible);
    loadDimensions(); m_timer->start(0);
}

void UnifiedQueueThumbnails::setCurrentPath(QString const& path)
{
    QSignalBlocker block(m_list);
    for (int i = 0; i < m_list->count(); ++i) {
        auto item = m_list->item(i); if (item->data(Path).toString() != path) continue;
        if (item->isHidden()) m_filter->setCurrentIndex(item->data(Kind).toString() == "processed" ? 0 : 1);
        m_list->setCurrentItem(item); m_list->scrollToItem(item, QAbstractItemView::EnsureVisible);
        m_timer->start(0); return;
    }
    m_list->setCurrentRow(-1);
}

void UnifiedQueueThumbnails::applyFilter()
{
    QSignalBlocker block(m_list); QString filter = m_filter->currentData().toString();
    for (int i = 0; i < m_list->count(); ++i) m_list->item(i)->setHidden(filter != "all" && m_list->item(i)->data(Kind).toString() != filter);
    m_timer->start(0);
}

void UnifiedQueueThumbnails::applyOrder()
{
    QSignalBlocker block(m_list);
    auto selected = m_list->currentItem(); QList<QListWidgetItem*> rows;
    while (m_list->count()) rows.append(m_list->takeItem(0));
    int order = m_order->currentIndex();
    std::stable_sort(rows.begin(), rows.end(), [order](auto a, auto b) {
        auto value = [order](auto item) {
            double width = item->data(Width).toDouble(), height = item->data(Height).toDouble();
            return order == 1 ? width : order == 2 ? height : height > 0 ? width / height : 0.;
        };
        double x = value(a), y = value(b);
        if (order && x != y) { if (x <= 0) return false; if (y <= 0) return true; return x < y; }
        return a->data(Natural).toInt() < b->data(Natural).toInt();
    });
    for (auto item : rows) m_list->addItem(item);
    if (selected) m_list->setCurrentItem(selected);
    applyFilter(); if (selected && !selected->isHidden()) m_list->scrollToItem(selected, QAbstractItemView::EnsureVisible);
}

bool UnifiedQueueThumbnails::eventFilter(QObject* object, QEvent* event)
{
    if (object == m_list->viewport()) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) m_timer->start(0);
    }
    return QWidget::eventFilter(object, event);
}

void UnifiedQueueThumbnails::loadVisible()
{
    if (!isVisible()) return;
    QRect nearby = m_list->viewport()->rect().adjusted(0, -260, 0, 260);
    QString nextPath, nextKey;
    for (int i = 0; i < m_list->count(); ++i) {
        auto item = m_list->item(i); QString key = item->data(CacheKey).toString();
        if (item->isHidden() || !nearby.intersects(m_list->visualItemRect(item))) {
            if (!item->icon().isNull()) item->setIcon(QIcon()); // bound retained pixmap memory
            continue;
        }
        if (auto image = m_cache.object(key)) {
            if (item->icon().isNull()) item->setIcon(QPixmap::fromImage(*image));
        } else if (nextPath.isEmpty() && !m_failed.contains(key)) {
            nextPath = item->data(Path).toString(); nextKey = key;
        }
    }
    if (m_loader || nextPath.isEmpty()) return;
    auto result = std::make_shared<QImage>(); quint64 generation = m_generation;
    m_loader = QThread::create([nextPath, result] {
        QImageReader reader(nextPath); reader.setAutoTransform(true);
        auto size = reader.size(); if (size.isValid()) reader.setScaledSize(size.scaled(220, 210, Qt::KeepAspectRatio));
        *result = reader.read();
        if (!result->isNull()) *result = result->scaled(220, 210, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    });
    connect(m_loader, &QThread::finished, this, [this, result, generation, nextKey] {
        m_loader->deleteLater(); m_loader = nullptr;
        if (generation == m_generation) {
            if (result->isNull()) m_failed.insert(nextKey); else m_cache.insert(nextKey, new QImage(*result));
        }
        m_timer->start(0);
    });
    m_loader->start();
}

void UnifiedQueueThumbnails::loadDimensions()
{
    if (m_metadata || m_order->currentIndex() == 0) return;
    QList<QPair<QString, QString>> missing;
    for (auto value : m_images) { auto image = value.toObject(); auto key = keyFor(image); if (!m_sizes.contains(key)) missing.append({key, image.value("path").toString()}); }
    if (missing.isEmpty()) return;
    quint64 generation = m_generation;
    auto sizes = std::make_shared<QMap<QString, QSize>>();
    m_metadata = QThread::create([missing, sizes] {
        for (auto pair : missing) {
            if (QThread::currentThread()->isInterruptionRequested()) break;
            sizes->insert(pair.first, QImageReader(pair.second).size());
        }
    });
    connect(m_metadata, &QThread::finished, this, [this, sizes, generation] {
        m_metadata->deleteLater(); m_metadata = nullptr;
        if (generation != m_generation) { loadDimensions(); return; }
        for (auto it = sizes->cbegin(); it != sizes->cend(); ++it) m_sizes.insert(it.key(), new QSize(it.value()));
        for (int i = 0; i < m_list->count(); ++i) {
            auto item = m_list->item(i); if (auto size = m_sizes.object(item->data(CacheKey).toString())) { item->setData(Width, size->width()); item->setData(Height, size->height()); }
        }
        applyOrder();
    });
    m_metadata->start();
}
