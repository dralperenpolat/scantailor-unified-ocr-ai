// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include <QJsonArray>
#include <QPointer>
#include <functional>
class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QThread;
class QVBoxLayout;
class BasicImageView;

class UnifiedQueuePreview : public QWidget {
public:
    explicit UnifiedQueuePreview(QWidget* parent = nullptr);
    ~UnifiedQueuePreview() override;
    void showSelection(QString const& title, QJsonArray const& images, QString const& path);
    void setCurrentPath(QString const& path);
    void showEditStatus(QString const& message);
    QString currentPath() const;
    std::function<void(QString const&)> currentPathChanged;
    std::function<void()> returnToEditor;
    std::function<void(int)> editRequested;
private:
    QLabel *m_title, *m_message, *m_editStatus;
    QComboBox* m_choices;
    QCheckBox* m_quality;
    QPushButton *m_previous, *m_next;
    QVBoxLayout* m_imageLayout;
    QPointer<BasicImageView> m_view;
    QThread* m_loader = nullptr;
    QString m_pendingPath;
    QString m_revision;
    quint64 m_generation = 0;
    void selectImage();
    void loadImage(bool fullResolution=false);
};
