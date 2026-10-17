// Copyright (C) 2015 Joseph Artsimovich <joseph.artsimovich@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef STAGELISTVIEW_H_
#define STAGELISTVIEW_H_
#include "IntrusivePtr.h"
#include <QTableView>
#include <QElapsedTimer>
#include <QHash>
class StageSequence;
class QToolButton;
class QStandardItemModel;
class StageListView : public QTableView {
    Q_OBJECT
public:
    explicit StageListView(QWidget* parent);
    ~StageListView() override;
    void setStages(IntrusivePtr<StageSequence> const& stages);
    void selectFilter(int filter);
    void selectStep(QString const& id);
    QString currentStep() const;
    QString stepId(int row) const;
    int filterForRow(int row) const;
    void setStepStatus(QString const& id,QString const& status);
    QSize sizeHint() const override {return m_sizeHint;}
signals:
    void launchBatchProcessing();
    void launchStep(QString const& id);
public slots:
    void setBatchProcessingPossible(bool possible);
    void setBatchProcessingInProgress(bool in_progress);
protected:
    void changeEvent(QEvent* event) override;
private:
    void updateMetrics();
    void updateButtons();
    QSize m_sizeHint;
    QStandardItemModel* m_model=nullptr;
    QHash<QString,QToolButton*> m_buttons;
    bool m_possible=false,m_running=false;
    QString m_runningStep;
    QElapsedTimer m_elapsed;
};
#endif
