// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QLabel>
#include <QTreeWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QSlider>
#include <QHash>
class UnifiedOcrOptions;
class QTimer;
class UnifiedPanel : public QWidget {
    Q_OBJECT
public:
    explicit UnifiedPanel(QWidget* parent=nullptr);
    QString snapshotPath();
    void addProject(QString const& title, QString const& path, QJsonArray const& outputs);
    bool busy() const;
    bool monitoringExternalWorker() const { return m_externalRunning; }
    void stop();
    QSize minimumSizeHint() const override { return QSize(300,140); }
    void setPanelHeight(int height);
    void setAppearance(QString const& appearance);
    QString appearance() const { return m_appearance->currentData().toString(); }
    bool cleanupEnabled() const { return m_cleanup->isChecked(); }
    void setCleanupEnabled(bool enabled);
    QJsonObject pdfLayoutOptions() const;
    void setPdfLayoutOptions(QJsonObject const& value);
    void openPdfTools(QString const& mode="import");
    void showToolsTab(int index);
    QString ocrTiming()const;
    void setOcrTiming(QString const& mode);
    QJsonObject outputFinish()const;
    void setOutputFinish(QJsonObject const& value);
    void setEditorContext(QString const& title, QString const& path);
    void clearPreviewSelection() { m_previewJob.clear(); m_previewPath.clear(); }
    void setPreviewPath(QString const& path) { m_previewPath = path; }
    void editPreview(int stage=3);
    QString editCopy(QJsonObject const& target) const;
    QString revisionSnapshotPath() const;
    bool submitRevision(QJsonObject const& binding, QString const& snapshot);
    void previewBook(QString const& id, int page=0);
signals:
    void ocrTimingChanged(QString const& mode);
    void outputFinishChanged(QJsonObject const& value);
    void pdfLayoutSettingsChanged(QJsonObject const& value);
    void pdfOutputSettingsChanged(QString const& appearance, bool cleanup);
    void panelHeightRequested(int height);
    void captureRequested();
    void openRequested(QString const& path);
    void editRequested(QJsonObject const& target, int stage);
    void settingsRequested();
    void accelerationChanged();
    void selectionPreviewRequested(QString const& title, QJsonArray const& images, QString const& path);
    void selectionPreviewUpdated(QString const& title, QJsonArray const& images, QString const& path);
private:
    void adoptJobRevision(QJsonObject const& before, QJsonObject const& after);
    friend class UnifiedQueueSelectionTest;
    QString m_root, m_queuePath, m_requestPath, m_controlPath, m_output;
    QJsonArray m_jobs;
    QHash<QString, QJsonArray> m_livePreviews;
    quint64 m_selectionRequest = 0;
    QString m_previewJob, m_previewPath;
    QTimer* m_previewRefresh = nullptr;
    QProcess m_worker, m_gpu;
    QByteArray m_buffer;
    bool m_paused=false;
    bool m_externalRunning=false;
    QString m_monitorRequest, m_monitorLog;
    qint64 m_monitorOffset=0;
    qint64 m_monitorQueueRead=0;
    void pollExternal();
    void updateRevisionStatus();
    QString m_revisionJob,m_revisionToken;
    bool m_revisionStartPending=false;
    QLabel* m_revisionStatus=nullptr;
    QLabel *m_editorContext=nullptr, *m_queueContext=nullptr;
    QPushButton* m_capture=nullptr;
    QTreeWidget *m_books, *m_pages;
    QComboBox *m_language, *m_appearance, *m_paper, *m_split, *m_ocrTiming;
    QJsonObject m_outputFinish;
    QSpinBox *m_threads, *m_cpu, *m_delay;
    QCheckBox *m_singleGpu, *m_numbers;
    QCheckBox *m_autoWarp, *m_cleanup, *m_fitContent;
    UnifiedOcrOptions* m_ocr = nullptr;
    QDoubleSpinBox *m_margin, *m_targetText;
    QLabel *m_status, *m_usage, *m_gpuUsage, *m_limits;
    QProgressBar* m_progress;
    QSlider* m_panelHeight;
    QPlainTextEdit* m_log;
    QPushButton *m_start, *m_prepare, *m_pause;
    void saveQueue();
    void loadQueue();
    void refreshBooks();
    void showBookSelection(QTreeWidgetItem* item);
    void showPageSelection(QTreeWidgetItem* item);
    void showSelection(QString const& jobId, QString const& path = {});
    void refreshPreview();
    void addFolder(QString const& folder);
    void start(bool prepare, bool revisionsOnly=false);
    void control(QString const& state);
    void receive();
    void event(QJsonObject const& object);
    void preview(QString const& path);
    void applyResources();
};
