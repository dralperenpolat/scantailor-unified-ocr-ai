// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QGroupBox>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QCheckBox>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QSettings>
#include <functional>

struct UnifiedStageReviewState {
    QString page,detail,selectedMethod,appliedMethod,nativeMethod;QStringList choices;int choice=-1,revision=-1,done=0,total=0;
    bool running=false,available=false,bulkAvailable=false,applied=false,manual=false,undo=false,stale=false,previewAvailable=false;
};

// Compact controls stay in the native stage's scrollable options pane.
// No image processing or project mutation lives in this widget.
class UnifiedStageReviewBar:public QGroupBox {
public:
    enum Action { Analyze,Preview,Apply,Undo,Stop,Advanced,Changed };
    std::function<void(Action)> action;
    explicit UnifiedStageReviewBar(bool geometry,QWidget* parent):QGroupBox(QString::fromUtf8("Öneri hazırla ve uygula"),parent),m_geometry(geometry){
        setObjectName(geometry?"stageGeometryAi":"stageContentAi");setMinimumWidth(0);setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);auto box=new QVBoxLayout(this);box->setAlignment(Qt::AlignTop);
        m_page=new QLabel;m_page->setObjectName("stageReviewPage");m_page->setWordWrap(true);box->addWidget(m_page);
        m_kind=new QComboBox;m_kind->setObjectName("stageReviewMethod");
        if(geometry)m_kind->addItem(QString::fromUtf8("Eğriliği düzelt · AI + metin + kenar"),"geometry");
        else {m_kind->addItem(QString::fromUtf8("Yazı alanını AI ile bul"),"ai");}
        m_kind->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);m_kind->setMinimumContentsLength(8);
        box->addWidget(new QLabel(QString::fromUtf8("1. İnceleme yöntemini çalıştır")));
        auto methodRow=new QHBoxLayout;methodRow->addWidget(m_kind,1);box->addLayout(methodRow);
        m_analyze=new QPushButton(QString::fromUtf8("▶ Hazırla"));m_analyze->setObjectName("stageReviewAnalyze");m_analyze->setAccessibleName(QString::fromUtf8("Seçili yöntemle öneri hazırla"));methodRow->addWidget(m_analyze);connect(m_analyze,&QPushButton::clicked,[this]{if(action)action(Analyze);});
        m_scopeLabel=new QLabel(QString::fromUtf8("İşlem: yalnız bu sayfa"));m_scopeLabel->setWordWrap(true);box->addWidget(m_scopeLabel);
        m_status=new QLabel(QString::fromUtf8("Önce öneri hazırla. Görüp beğenirsen uygula."));m_status->setObjectName("stageReviewStatus");m_status->setWordWrap(true);box->addWidget(m_status);
        m_progress=new QProgressBar;m_progress->setTextVisible(true);m_progress->hide();box->addWidget(m_progress);
        box->addWidget(new QLabel(QString::fromUtf8("2. Öneri seç ve uygula")));
        auto choiceRow=new QHBoxLayout;box->addLayout(choiceRow);
        m_choice=new QComboBox;m_choice->setObjectName("stageReviewCandidate");m_choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);m_choice->setMinimumContentsLength(8);choiceRow->addWidget(m_choice,1);
        m_apply=new QPushButton(QString::fromUtf8("▶ Uygula"));m_apply->setObjectName("stageReviewApply");m_apply->setAccessibleName(QString::fromUtf8("Seçili adayı uygula"));m_apply->setEnabled(false);choiceRow->addWidget(m_apply);connect(m_apply,&QPushButton::clicked,[this]{if(action)action(Apply);});
        m_selected=new QLabel;m_selected->setObjectName("stageReviewSelected");m_selected->setWordWrap(true);box->addWidget(m_selected);
        m_applied=new QLabel;m_applied->setObjectName("stageReviewApplied");m_applied->setWordWrap(true);box->addWidget(m_applied);
        m_manual=new QCheckBox(QString::fromUtf8("Elle ayarım da değişebilir"));m_manual->setObjectName("stageReviewAllowManual");m_manual->setToolTip(QString::fromUtf8("Yalnız bu sayfa için izin verir. Toplu işlem elle düzenlenmiş sayfaları korur."));m_manual->hide();box->addWidget(m_manual);
        auto buttons=new QGridLayout;box->addLayout(buttons);
        auto button=[&](char const* name,char const* text,Action a,int row,int col){auto b=new QPushButton(QString::fromUtf8(text));b->setObjectName(name);buttons->addWidget(b,row,col);connect(b,&QPushButton::clicked,[this,a]{if(action)action(a);});return b;};
        m_preview=button("stageReviewPreview","Öneriyi göster",Preview,0,0);m_preview->setCheckable(true);m_preview->setEnabled(false);
        m_stop=button("stageReviewStop","Durdur",Stop,0,1);m_stop->setEnabled(false);
        m_undo=new QPushButton(QString::fromUtf8("Son uygulamayı geri al"));m_undo->setObjectName("stageReviewUndo");m_undo->setEnabled(false);box->addWidget(m_undo);connect(m_undo,&QPushButton::clicked,[this]{if(action)action(Undo);});
        m_analyze->setToolTip(QString::fromUtf8("Sayfayı inceler, projeyi değiştirmez. Ctrl+Alt+A"));m_preview->setToolTip(QString::fromUtf8("Öneriyi ana görüntüde gösterir. Tekrar basınca düzenleyiciye döner. Ctrl+Alt+P"));m_apply->setToolTip(QString::fromUtf8("Hazır öneriyi seçili kapsama uygular. Ctrl+Alt+Enter"));m_undo->setToolTip(QString::fromUtf8("Son tekli veya toplu uygulamayı geri alır. Ctrl+Alt+Backspace"));
        m_scope=new QComboBox;m_scope->setObjectName("stageReviewScope");m_scope->addItem(QString::fromUtf8("Yalnız bu sayfa"),"page");m_scope->addItem(QString::fromUtf8("Sağda seçtiğim sayfalar"),"selected");m_scope->addItem(QString::fromUtf8("Kitabın tamamı"),"book");box->addWidget(new QLabel(QString::fromUtf8("Hangi sayfalara?")));box->addWidget(m_scope);
        auto more=new QToolButton;more->setText(QString::fromUtf8("İşlemci ve ayrıntılar"));more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);more->setArrowType(Qt::RightArrow);more->setCheckable(true);more->setObjectName("stageReviewMore");box->addWidget(more);
        auto advanced=new QWidget;auto advancedBox=new QVBoxLayout(advanced);advancedBox->setContentsMargins(0,0,0,0);box->addWidget(advanced);advanced->hide();
        m_device=new QComboBox;m_device->setObjectName("stageReviewDevice");m_device->addItem(QString::fromUtf8("İşlemci · CPU"),"cpu");m_device->addItem(QString::fromUtf8("Ekran kartı · GPU"),"gpu");m_device->setCurrentIndex(QSettings().value("unified/ocr_device").toString().startsWith("gpu")?1:0);advancedBox->addWidget(m_device);
        auto detail=new QPushButton(QString::fromUtf8("Ayrıntılı karşılaştırmayı aç…"));advancedBox->addWidget(detail);connect(detail,&QPushButton::clicked,[this]{if(action)action(Advanced);});
        auto help=new QLabel(QString::fromUtf8("Toplu işlemde yalnız uygun öneriler uygulanır. Elle değişen sayfalar korunur. Uyguladıklarını Ctrl+S ile kaydet; PDF için Kaydet ve PDF’yi güncelle’yi kullan."));help->setWordWrap(true);advancedBox->addWidget(help);
        auto shortcuts=new QLabel(QString::fromUtf8("Kısayollar: Ctrl+Alt+A öneri · Ctrl+Alt+P önizleme · Ctrl+Alt+Enter uygula · Ctrl+Alt+Backspace geri al"));shortcuts->setWordWrap(true);advancedBox->addWidget(shortcuts);
        connect(more,&QToolButton::toggled,[=](bool checked){advanced->setVisible(checked);more->setArrowType(checked?Qt::DownArrow:Qt::RightArrow);});
        connect(m_kind,qOverload<int>(&QComboBox::currentIndexChanged),[this]{resetView();if(action)action(Changed);});
        m_native=new QLabel;m_native->setObjectName("stageNativePlayHelp");m_native->setWordWrap(true);box->addWidget(m_native);
        connect(m_scope,qOverload<int>(&QComboBox::currentIndexChanged),[this]{m_scopeLabel->setText(QString::fromUtf8("İşlem: ")+m_scope->currentText());m_apply->setText(scope()=="page"?QString::fromUtf8("▶ Uygula"):QString::fromUtf8("▶ Toplu"));m_apply->setToolTip(scope()=="page"?QString::fromUtf8("Seçili adayı yalnız bu sayfaya uygula. Ctrl+Alt+Enter"):QString::fromUtf8("Kapsamdaki her sayfanın kendi önerilen adayını uygula; elle ayarlar korunur."));m_manual->setChecked(false);if(action)action(Changed);});
        connect(m_choice,qOverload<int>(&QComboBox::currentIndexChanged),[this]{if(action)action(Changed);if(m_preview->isChecked()&&action)action(Preview);});
        connect(m_manual,&QCheckBox::toggled,[this]{if(action)action(Changed);});
    }
    QString kind()const{return m_kind->currentData().toString();}QString scope()const{return m_scope->currentData().toString();}QString device()const{return m_device->currentData().toString();}
    int candidate()const{return m_choice->currentIndex();}bool allowManual()const{return scope()=="page"&&m_manual->isChecked();}bool previewing()const{return m_preview->isChecked();}
    int candidateFor(QString const& page)const{return m_pageKey==page?candidate():-1;}
    void resetView(){QSignalBlocker b(m_preview),c(m_manual);m_preview->setChecked(false);m_preview->setText(QString::fromUtf8("Öneriyi göster"));m_manual->setChecked(false);m_revision=-2;}
    void setPreview(bool on){QSignalBlocker b(m_preview);m_preview->setChecked(on);m_preview->setText(on?QString::fromUtf8("Düzenleyiciye dön"):QString::fromUtf8("Öneriyi göster"));}
    void showState(UnifiedStageReviewState const& s,QString const& feedback={}){
        if(m_pageKey!=s.page){resetView();m_pageKey=s.page;}m_page->setText(s.page);m_status->setText(feedback.isEmpty()?s.detail:feedback);
        m_selected->setText(scope()=="page"?QString::fromUtf8("Seçili: ")+(s.selectedMethod.isEmpty()?QString::fromUtf8("henüz öneri yok"):s.selectedMethod):QString::fromUtf8("Toplu işlem: her sayfanın kendi önerilen adayı."));
        m_applied->setText(QString::fromUtf8("Bu sayfada uygulanan: ")+(s.stale?QString::fromUtf8("eski öneri geçersiz; yeniden hesaplanmalı"):s.appliedMethod.isEmpty()?QString::fromUtf8("henüz uygulanmadı"):s.appliedMethod));
        m_native->setText(QString::fromUtf8("Stages yanındaki üst ▶: kitabı bu aşamaya kadar kayıtlı Auto / Manual ayarlarıyla işler. AI için buradaki ▶ düğmelerini kullan.")+(s.nativeMethod.isEmpty()?QString():QString::fromUtf8("\nBu sayfanın kayıtlı ayarı: ")+s.nativeMethod));
        if(m_revision!=s.revision||m_choices!=s.choices){QSignalBlocker b(m_choice);m_choice->clear();m_choice->addItems(s.choices);m_choice->setCurrentIndex(s.choice);m_revision=s.revision;m_choices=s.choices;}
        m_choice->setEnabled(!s.choices.isEmpty()&&!s.stale);m_manual->setVisible(s.manual&&scope()=="page");
        m_analyze->setEnabled(!s.running&&!s.page.isEmpty());m_stop->setEnabled(s.running);m_device->setEnabled(!s.running);m_kind->setEnabled(!s.running);
        m_progress->setVisible(s.running);m_progress->setRange(0,s.total);m_progress->setValue(s.done);
        m_preview->setEnabled(s.previewAvailable&&!s.stale);m_apply->setEnabled(scope()=="page"?s.available&&!s.applied&&(!s.manual||allowManual()):s.bulkAvailable);m_undo->setEnabled(s.undo);
    }
    void trigger(Action a){auto b=a==Analyze?m_analyze:a==Preview?m_preview:a==Apply?m_apply:a==Undo?m_undo:m_stop;if(b->isEnabled())b->click();}
private:
    bool m_geometry;QString m_pageKey;int m_revision=-2;QStringList m_choices;QLabel *m_page,*m_scopeLabel,*m_status,*m_selected,*m_applied,*m_native;QProgressBar* m_progress;QComboBox *m_kind,*m_scope,*m_choice,*m_device;QCheckBox* m_manual;QPushButton *m_analyze,*m_preview,*m_apply,*m_undo,*m_stop;
};
