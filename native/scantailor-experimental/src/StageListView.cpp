// Copyright (C) 2015 Joseph Artsimovich <joseph.artsimovich@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StageListView.h"
#include "StageSequence.h"
#include "UnifiedWorkflow.h"
#include <QHeaderView>
#include <QStandardItemModel>
#include <QToolButton>
#include <QTimer>
#include <QEvent>
#include <QStyle>
#include <QFontMetrics>
StageListView::StageListView(QWidget* parent):QTableView(parent) {
    setObjectName("filterList");setSelectionBehavior(SelectRows);setSelectionMode(SingleSelection);
    setEditTriggers(NoEditTriggers);setWordWrap(false);setTextElideMode(Qt::ElideNone);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    horizontalHeader()->hide();verticalHeader()->hide();setShowGrid(false);
    auto timer=new QTimer(this);timer->setInterval(1000);
    connect(timer,&QTimer::timeout,this,[this]{if(m_running)setStepStatus(m_runningStep,unified_workflow::duration(m_elapsed.elapsed()));});timer->start();
}
StageListView::~StageListView()=default;
void StageListView::setStages(IntrusivePtr<StageSequence> const& stages) {
    Q_UNUSED(stages);
    if(m_model)m_model->deleteLater();
    m_buttons.clear();
    m_editorStatuses.clear();m_queueView=false;
    auto list=unified_workflow::steps();m_model=new QStandardItemModel(list.size(),3,this);setModel(m_model);
    for(int i=0;i<list.size();++i){auto const step=list[i];
        auto item=new QStandardItem(step.title);item->setToolTip(step.description);item->setData(step.id,Qt::UserRole);m_model->setItem(i,0,item);
        m_model->setItem(i,1,new QStandardItem);
        if(!step.playable)continue;
        auto button=new QToolButton;button->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));button->setObjectName("stagePlay_"+step.id);
        button->setToolTip(step.description);button->setAccessibleName(step.title+" · "+unified_workflow::text("Çalıştır","Run"));
        button->setAutoRaise(false);setIndexWidget(m_model->index(i,2),button);m_buttons.insert(step.id,button);
        connect(button,&QToolButton::clicked,this,[this,step]{selectStep(step.id);emit launchStep(step.id);});
    }
    horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    horizontalHeader()->setSectionResizeMode(1,QHeaderView::Fixed);horizontalHeader()->setSectionResizeMode(2,QHeaderView::Fixed);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);updateVisibility();updateButtons();
}
void StageListView::updateMetrics(){
    int width=0;const QFontMetrics fm(font());for(auto const& step:unified_workflow::steps())width=qMax(width,fm.horizontalAdvance(step.title));
    int row=qMax(28,fm.height()+12);verticalHeader()->setDefaultSectionSize(row);
    horizontalHeader()->resizeSection(1,fm.horizontalAdvance("9999 s")+12);horizontalHeader()->resizeSection(2,row+4);
    int visible=0;for(int i=0;i<m_model->rowCount();++i)if(!isRowHidden(i))++visible;
    m_sizeHint=QSize(width+horizontalHeader()->sectionSize(1)+row+30,visible*row+4);
    setMinimumWidth(m_sizeHint.width());setFixedHeight(m_sizeHint.height());setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);updateGeometry();
}
void StageListView::changeEvent(QEvent* event){QTableView::changeEvent(event);if(event->type()==QEvent::FontChange&&m_model)updateMetrics();}
void StageListView::selectFilter(int filter){auto list=unified_workflow::steps();for(int i=0;i<list.size();++i)if(list[i].filter==filter){selectStep(list[i].id);return;}}
void StageListView::selectStep(QString const& id){
    QString visible=(!m_aiEnabled&&id.startsWith("ai_"))?(id=="ai_geometry"?"geometry":"content"):id;
    int row=unified_workflow::row(visible);if(row>=0){setNavigationGroup(groupForStep(visible));selectRow(row);}
}
int StageListView::groupForStep(QString const& id){
    if(id=="input")return 0;
    if(id=="cleanup")return 2;
    if(id=="pdf"||id=="ocr")return 3;
    if(id=="storage"||id=="settings"||id=="help")return 4;
    return 1;
}
void StageListView::updateVisibility(){
    if(!m_model)return;
    for(int i=0;i<m_model->rowCount();++i)setRowHidden(i,(!m_aiEnabled&&stepId(i).startsWith("ai_"))||(!m_queueView&&!m_showAll&&groupForStep(stepId(i))!=m_group));
    updateMetrics();
}
void StageListView::setNavigationGroup(int group){m_group=group;updateVisibility();emit navigationGroupChanged(group);}
void StageListView::setAiEnabled(bool enabled){
    m_aiEnabled=enabled;
    if(!enabled&&currentStep().startsWith("ai_"))selectStep(currentStep()=="ai_geometry"?"geometry":"content");
    updateVisibility();updateButtons();
}
void StageListView::showAllStages(bool show){
    m_showAll=show;if(!show&&!currentStep().isEmpty())m_group=groupForStep(currentStep());
    updateVisibility();emit navigationGroupChanged(m_group);
}
QString StageListView::currentStep()const{return stepId(currentIndex().row());}
QString StageListView::stepId(int row)const{auto list=unified_workflow::steps();return row>=0&&row<list.size()?list[row].id:QString();}
int StageListView::filterForRow(int row)const{auto list=unified_workflow::steps();return row>=0&&row<list.size()?list[row].filter:-1;}
void StageListView::setStepStatus(QString const& id,QString const& status){
    m_editorStatuses.insert(id,status);int row=unified_workflow::row(id);if(row<0||!m_model||m_queueView)return;
    m_model->item(row,1)->setText(status);m_model->item(row,1)->setToolTip(status);
}
void StageListView::showQueueProgress(QHash<QString,QString> const& statuses,QString const& active){
    if(!m_model)return;
    bool changed=!m_queueView;m_queueView=true;
    for(int i=0;i<m_model->rowCount();++i){
        auto id=stepId(i);m_model->item(i,1)->setText(statuses.value(id));m_model->item(i,1)->setToolTip(statuses.value(id));
        auto font=m_model->item(i,0)->font();font.setBold(id==active);m_model->item(i,0)->setFont(font);
    }
    if(changed){updateVisibility();updateButtons();}
}
void StageListView::clearQueueProgress(){
    if(!m_queueView)return;m_queueView=false;
    for(int i=0;i<m_model->rowCount();++i){
        setStepStatus(stepId(i),m_editorStatuses.value(stepId(i)));
        auto font=m_model->item(i,0)->font();font.setBold(false);m_model->item(i,0)->setFont(font);
    }
    updateVisibility();updateButtons();
}
void StageListView::updateButtons(){for(auto b:m_buttons)b->setEnabled(m_possible&&!m_running&&!m_queueView);}
void StageListView::setBatchProcessingPossible(bool possible){m_possible=possible;updateButtons();}
void StageListView::setBatchProcessingInProgress(bool running){
    if(running==m_running)return;
    m_running=running;
    if(running){m_runningStep=currentStep();m_elapsed.start();}
    else if(m_elapsed.isValid())setStepStatus(m_runningStep,unified_workflow::duration(m_elapsed.elapsed()));
    updateButtons();
}
