// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include <QCache>
#include <QImage>
#include <QJsonArray>
#include <QSet>
#include <functional>
class QComboBox;
class QLabel;
class QListWidget;
class QThread;
class QTimer;

class UnifiedQueueThumbnails : public QWidget {
public:
    explicit UnifiedQueueThumbnails(QWidget* parent = nullptr);
    ~UnifiedQueueThumbnails() override;
    void setImages(QJsonArray const& images, QString const& selectedPath);
    void setCurrentPath(QString const& path);
    std::function<void(QString const&)> selectionChanged;
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    QComboBox *m_filter, *m_order;
    QLabel* m_note;
    QListWidget* m_list;
    QTimer* m_timer;
    QThread *m_loader = nullptr, *m_metadata = nullptr;
    QJsonArray m_images;
    QCache<QString, QImage> m_cache{64};
    QCache<QString, QSize> m_sizes{5000};
    QSet<QString> m_failed;
    quint64 m_generation = 0;
    void applyFilter();
    void applyOrder();
    void loadVisible();
    void loadDimensions();
};
