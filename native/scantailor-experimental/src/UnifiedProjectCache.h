// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UnifiedWorkflow.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>
#include <functional>
#include <algorithm>

// Only explicitly owned project folders are scanned. Deletion is per file,
// with a fresh identity / containment check, never a recursive folder removal.
class UnifiedProjectCache : public QWidget {
    struct Entry { QString path, root, kind; qint64 bytes, modified; bool protectedFile; };
    QString m_project, m_savedProject, m_pdf, m_pdfHash;
    QJsonObject m_registry;
    QJsonArray m_pages;
    QTableWidget* m_table;
    QComboBox* m_when;
    QCheckBox* m_discard;
    QLabel *m_summary, *m_status;
    QMap<QString,QCheckBox*> m_keep;
    QVector<Entry> m_entries;
    bool m_loading=false;
    const QStringList m_kinds={"sources","native","ocr","previews","temporary","protected"};
    static QJsonObject read(QString const& path) {
        QFile f(path);return f.open(QIODevice::ReadOnly)?QJsonDocument::fromJson(f.readAll()).object():QJsonObject();
    }
    static bool write(QString const& path,QJsonObject const& value) {
        QSaveFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(value).toJson())>=0&&f.commit();
    }
    static Qt::CaseSensitivity pathCase(){
#ifdef Q_OS_WIN
        return Qt::CaseInsensitive;
#else
        return Qt::CaseSensitive;
#endif
    }
    static bool contained(QString const& file,QString const& root) {
        QFileInfo f(file), r(root);
        if(f.isSymLink()||r.isSymLink()||!r.isDir()||!f.isFile())return false;
        auto canonical=f.canonicalFilePath(), base=r.canonicalFilePath();
        if(canonical.isEmpty()||base.isEmpty()||!canonical.startsWith(base+"/",pathCase()))return false;
        // Reject junction/symlink ancestors too, including those inside root.
        for(auto parent=f.absoluteDir();;){
            if(QFileInfo(parent.absolutePath()).isSymLink())return false;
            if(parent.isRoot()||!parent.cdUp())break;
        }
        return true;
    }
    bool owns(QString const& root) const {
        return !m_project.isEmpty()&&read(root+"/.stou-cache-owner.json").value("project").toString()==m_project;
    }
    bool saved() const {
        if(!m_savedProject.isEmpty()&&QFileInfo::exists(m_savedProject))return true;
        for(auto path:m_registry.value("saved_projects").toArray())if(QFileInfo::exists(path.toString()))return true;
        return false;
    }
    bool removable(Entry const& e,QString const& event) const {
        bool discard=event=="discard"&&m_discard->isChecked()&&!saved();
        bool releaseInputs=(event=="close"||event=="discard")&&!saved()&&(discard||!m_keep["sources"]->isChecked());
        if(e.protectedFile&&!((e.kind=="sources"&&releaseInputs)||(e.kind=="edits"&&discard)))return false;
        if(e.kind=="sources")return releaseInputs; // Active/saved project inputs are dependencies.
        if(e.kind=="edits")return discard;
        if(event=="discard"&&e.kind=="native"&&!e.path.endsWith(".ocr.tif",pathCase()))return false;
        if(discard)return true;
        return m_keep.contains(e.kind)&&!m_keep[e.kind]->isChecked();
    }
    void persistPolicy() {
        if(m_loading)return;
        QJsonObject keep;for(auto it=m_keep.begin();it!=m_keep.end();++it)keep.insert(it.key(),it.value()->isChecked());
        QJsonObject value{{"when",m_when->currentData().toString()},{"discard_unsaved",m_discard->isChecked()},{"keep",keep}};
        QSettings().setValue("unified/projectCachePolicy",QJsonDocument(value).toJson(QJsonDocument::Compact));
        m_registry.insert("policy",value);save();updateSummary();
    }
    void loadPolicy(QJsonObject value) {
        m_loading=true;
        if(value.isEmpty())value=QJsonDocument::fromJson(QSettings().value("unified/projectCachePolicy").toByteArray()).object();
        m_when->setCurrentIndex(qMax(0,m_when->findData(value.value("when").toString("manual"))));
        m_discard->setChecked(value.value("discard_unsaved").toBool(true));
        auto keep=value.value("keep").toObject();
        for(auto it=m_keep.begin();it!=m_keep.end();++it)it.value()->setChecked(keep.value(it.key()).toBool(it.key()=="native"||it.key()=="ocr"||it.key()=="sources"));
        m_loading=false;
    }
    void save(){
        if(m_project.isEmpty())return;
        auto disk=read(m_project+"/.project-cache.json");
        for(auto key:{"roots","saved_projects","pdfs"}){auto rows=m_registry.value(key).toArray();for(auto value:disk.value(key).toArray())if(!rows.contains(value))rows.append(value);m_registry.insert(key,rows);}
        write(m_project+"/.project-cache.json",m_registry);
    }
    void add(QString path,QString const& root,QString kind,bool protectedFile=false) {
        if(!contained(path,root))return;
        path=QFileInfo(path).absoluteFilePath();for(auto const& e:m_entries)if(e.path==path)return;
        QFileInfo f(path);m_entries.push_back({path,root,kind,f.size(),f.lastModified().toMSecsSinceEpoch(),protectedFile});
    }
    void updateSummary() {
        using unified_workflow::text;
        qint64 total=0, reclaim=0, protectedBytes=0;
        QMap<QString,qint64> sizes;
        for(auto const& e:m_entries){total+=e.bytes;sizes[e.kind=="edits"?"protected":e.kind]+=e.bytes;
            if(removable(e,"manual"))reclaim+=e.bytes;else protectedBytes+=e.bytes;}
        for(int i=0;i<m_kinds.size();++i)m_table->item(i,2)->setText(sizeText(sizes[m_kinds[i]]));
        m_summary->setText(text("Bu proje: ","This project: ")+sizeText(total)+text(" · seçime göre silinebilir: "," · selected for removal: ")+sizeText(reclaim)+text(" · saklanacak/korunan: "," · retained/protected: ")+sizeText(protectedBytes)+"\n"+m_project);
        m_summary->setToolTip(text("Yalnız bu uygulamanın sahipliğini kaydettiği dosyalar sayılır. Kaynak kitaplar, başka projeler, modeller ve derleme klasörleri bu temizliğe dahil değildir.","Only files whose ownership is recorded by this app are counted. Source books, other projects, models and build folders are outside this cleanup."));
    }
public:
    std::function<void()> cleanRequested;
    std::function<bool()> busy;
    static QString sizeText(qint64 bytes) {
        if(bytes>=1024ll*1024*1024)return QString::number(double(bytes)/(1024*1024*1024),'f',2)+" GiB";
        if(bytes>=1024*1024)return QString::number(double(bytes)/(1024*1024),'f',1)+" MiB";
        if(bytes>=1024)return QString::number(double(bytes)/1024,'f',1)+" KiB";
        return QString::number(bytes)+" B";
    }
    explicit UnifiedProjectCache(QWidget* parent=nullptr):QWidget(parent) {
        using unified_workflow::text;
        setObjectName("workflowCache");auto layout=new QVBoxLayout(this);
        auto heading=new QLabel(text("12 · Önbellek ve son temizlik","12 · Cache and final cleanup"));layout->addWidget(heading);
        auto description=new QLabel(text("Sakla işaretli kategoriler korunur. İşareti kaldırılanlar seçilen zamanda temizlenir. Kaynak kitaplar ve nihai PDF’ler silinmez. Proje açıkken veya kaydedilmişse gerekli girdi resimleri ve onaylı düzeltmeler korunur.","Checked categories are retained. Unchecked categories are cleaned at the selected time. Original books and final PDFs are never deleted. Required input images and approved corrections are protected while the project is open or saved."));description->setWordWrap(true);layout->addWidget(description);
        m_when=new QComboBox;m_when->setObjectName("workflowCacheWhen");
        m_when->addItem(text("Yalnız ben Temizle’ye basınca","Only when I click Clean"),"manual");
        m_when->addItem(text("Başarılı PDF oluşturma sonrasında","After a successful PDF export"),"pdf");
        m_when->addItem(text("Başarılı OCR sonrasında","After successful OCR"),"ocr");
        m_when->addItem(text("Proje kapanırken","When the project closes"),"close");
        layout->addWidget(new QLabel(text("Temizlik zamanı","Cleanup timing")));layout->addWidget(m_when);
        m_discard=new QCheckBox(text("Kaydetmeden kapatılan yeni projenin geçici dosyalarını sil; Çıktı resimlerini ve PDF’leri koru","Delete temporary files when a new project closes without saving; retain Output images and PDFs"));m_discard->setObjectName("workflowCacheDiscard");layout->addWidget(m_discard);
        m_discard->setToolTip(text("Bu seçenek saklama kutularından önceliklidir. Önceden kaydedilmiş projenin girdileri ve düzeltmeleri silinmez. Kaydet veya İptal ile çıkarsanız bu kural çalışmaz.","This overrides the retention boxes. Inputs and corrections of a previously saved project remain protected. This rule does not run when you save or cancel closing."));
        m_table=new QTableWidget(m_kinds.size(),4);m_table->setObjectName("workflowCacheTable");m_table->setHorizontalHeaderLabels({text("Sakla","Keep"),text("Kategori","Category"),text("Boyut","Size"),text("Silinirse ne olur?","What happens if deleted?")});m_table->verticalHeader()->hide();m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);m_table->setWordWrap(true);
        QStringList titles={text("PDF’den çıkan / kopyalanan girdiler","Extracted / copied input images"),text("Ara TIFF ve OCR görüntüleri","Intermediate TIFF and OCR images"),text("OCR sonuç önbelleği","OCR result cache"),text("Önizlemeler","Previews"),text("Geçici PDF ve işlem dosyaları","Temporary PDF and process files"),text("Nihai çıktılar ve proje kayıtları","Final outputs and project records")};
        QStringList effects={text("Açık/kayıtlı projede korunur. Kaydedilmemiş proje kapatılırsa PDF’den tekrar içe aktarmak gerekir.","Protected for open/saved projects. After discarding a new project, import the PDF again."),text("Çıktı aşaması gerektiğinde yeniden çalışır. Son PDF korunur. Kaydetmeden çıkış, görünür Çıktı TIFF’lerini korur.","Output runs again when needed; the final PDF remains. Closing without saving retains visible Output TIFFs."),text("Son PDF’nin metni kalır. Aynı görüntüye yeniden OCR istenirse tanıma daha uzun sürer. Modeller korunur.","Text already in the final PDF remains. Repeating OCR may take longer. Models are kept."),text("Küçük resimler ve önce/sonra görüntüleri yeniden hazırlanır; onaylı maskeler korunur.","Thumbnails and before/after images must be regenerated; approved masks remain."),text("Bitmiş PDF etkilenmez. Yarım kalan dışa aktarma yeniden başlatılır.","Finished PDFs are unaffected. An interrupted export must restart."),text("Silinmez: nihai PDF’ler, sürümleri, sayfa güncelleme kayıtları ve kayıtlı projenin düzeltmeleri.","Never removed: final PDFs, versions, page-update records and saved project corrections.")};
        for(int i=0;i<m_kinds.size();++i){
            if(m_kinds[i]!="protected"){auto check=new QCheckBox;check->setObjectName("workflowCacheKeep_"+m_kinds[i]);m_table->setCellWidget(i,0,check);m_keep.insert(m_kinds[i],check);connect(check,&QCheckBox::toggled,this,[this]{persistPolicy();});}
            m_table->setItem(i,1,new QTableWidgetItem(titles[i]));m_table->setItem(i,2,new QTableWidgetItem("0 B"));auto item=new QTableWidgetItem(effects[i]);item->setToolTip(effects[i]);m_table->setItem(i,3,item);
        }
        m_table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);m_table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Interactive);m_table->setColumnWidth(1,190);m_table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::ResizeToContents);m_table->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);m_table->verticalHeader()->setDefaultSectionSize(76);layout->addWidget(m_table,1);
        m_summary=new QLabel;m_summary->setWordWrap(true);m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(m_summary);
        auto buttons=new QHBoxLayout;layout->addLayout(buttons);auto refresh=new QPushButton(text("Boyutları yenile","Refresh sizes"));refresh->setObjectName("workflowCacheRefresh");buttons->addWidget(refresh);connect(refresh,&QPushButton::clicked,this,[this]{scan();});
        auto clean=new QPushButton(text("Seçime göre temizle","Clean selected categories"));clean->setObjectName("workflowCacheClean");buttons->addWidget(clean);connect(clean,&QPushButton::clicked,this,[this]{if(cleanRequested)cleanRequested();});
        m_status=new QLabel;m_status->setWordWrap(true);m_status->setObjectName("workflowCacheStatus");layout->addWidget(m_status);
        connect(m_when,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{persistPolicy();});connect(m_discard,&QCheckBox::toggled,this,[this]{persistPolicy();});loadPolicy({});updateSummary();
    }
    void context(QString const& project,QString const& savedProject,QJsonArray const& pages,QString const& pdf,QString const& hash) {
        QString key=project.isEmpty()?QString():QDir::fromNativeSeparators(QFileInfo(project).absoluteFilePath());
        if(key!=m_project){m_project=key;m_registry=key.isEmpty()?QJsonObject():read(key+"/.project-cache.json");m_entries.clear();loadPolicy(m_registry.value("policy").toObject());if(!key.isEmpty()&&!m_registry.contains("policy"))persistPolicy();}
        m_savedProject=savedProject;m_pages=pages;m_pdf=pdf;m_pdfHash=hash;
        if(!savedProject.isEmpty())markSaved(savedProject);
    }
    void markSaved(QString const& path) {
        if(m_project.isEmpty()||path.isEmpty()||!QFileInfo::exists(path))return;
        auto paths=m_registry.value("saved_projects").toArray();if(!paths.contains(path)){paths.append(path);m_registry.insert("saved_projects",paths);save();}
    }
    // Call only for a folder created exclusively by this workflow.
    bool own(QString const& folder,QString const& role) {
        if(m_project.isEmpty()||QFileInfo(folder).isSymLink()||!QFileInfo(folder).isDir())return false;
        auto marker=folder+"/.stou-cache-owner.json";auto existing=read(marker);
        if(!existing.isEmpty()&&existing.value("project").toString()!=m_project)return false;
        if(!write(marker,{{"project",m_project},{"role",role}}))return false;
        auto roots=m_registry.value("roots").toArray();QJsonObject row{{"path",QFileInfo(folder).absoluteFilePath()},{"role",role}};if(!roots.contains(row))roots.append(row);m_registry.insert("roots",roots);save();return true;
    }
    void scan() {
        m_entries.clear();
        if(!m_project.isEmpty())m_registry=read(m_project+"/.project-cache.json");
        if(m_project.isEmpty()){updateSummary();return;}
        for(auto value:m_registry.value("roots").toArray()){
            auto row=value.toObject();auto root=row.value("path").toString(),role=row.value("role").toString();if(!owns(root))continue;
            if(role=="native"){
                for(auto value:m_pages){auto page=value.toObject();for(auto key:{"source","gray"})add(page.value(key).toString(),root,"native");}
                QDirIterator thumbs(root+"/cache/thumbs",{"*.png"},QDir::Files);while(thumbs.hasNext())add(thumbs.next(),root,"previews");
                for(auto name:{".workflow-state.json",".project-cache.json"})add(root+"/"+name,root,"protected",true);
                continue;
            }
            QDirIterator it(root,QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
            while(it.hasNext()){
                auto path=it.next();auto name=QFileInfo(path).fileName();if(name.startsWith(".stou-"))continue;
                if(role=="sources"){
                    bool generated=name=="import.json"||(name.startsWith("page-")&&(name.endsWith(".png")||name.endsWith(".json")));
                    add(path,root,generated?"sources":"protected",true);
                }
                else if(role=="ocr")add(path,root,"ocr");
                else if(role=="temporary")add(path,root,"temporary");
                else if(name.endsWith("-mask.png")||name=="cleanup.json")add(path,root,"edits",true);
                else if(name.endsWith("-before.png")||name.endsWith("-after.png"))add(path,root,"previews");
                else if(name=="request.json"||name=="result.json"||name=="cancel"||name.endsWith(".part.pdf")||name.startsWith("ocr-pages-")||name.startsWith(".smart-"))add(path,root,"temporary");
                else add(path,root,"protected",true);
            }
        }
        if(!owns(m_project))for(auto value:m_pages){auto page=value.toObject();for(auto key:{"source","gray"})add(page.value(key).toString(),m_project,"protected",true);}
        for(auto value:m_registry.value("pdfs").toArray()){
            auto path=value.toString();add(path,QFileInfo(path).absolutePath(),"protected",true);add(QFileInfo(path).absolutePath()+"/"+QFileInfo(path).completeBaseName()+".workflow.json",QFileInfo(path).absolutePath(),"protected",true);
        }
        updateSummary();
    }
    void producedPdf(QString const& path){auto paths=m_registry.value("pdfs").toArray();if(!paths.contains(path))paths.append(path);m_registry.insert("pdfs",paths);save();}
    QJsonObject clean(QString const& event="manual") {
        using unified_workflow::text;
        if(busy&&busy()){m_status->setText(text("İşlem sürüyor; temizlik için tamamlanmasını bekleyin.","Processing is active; wait before cleaning."));return {};}
        if(event!="manual"&&event!="discard"&&m_when->currentData()!=event)return {};
        if(event=="discard"&&(!m_discard->isChecked()||saved())&&m_when->currentData()!="close")return {};
        scan();qint64 removed=0;int count=0,failed=0;
        bool needsPdf=false;for(auto const& e:m_entries)if(removable(e,event)&&e.kind=="native"&&!e.path.endsWith(".ocr.tif",Qt::CaseInsensitive))needsPdf=true;
        bool validPdf=!needsPdf;
        if(needsPdf&&!m_pdf.isEmpty()&&!m_pdfHash.isEmpty()){QFile f(m_pdf);QCryptographicHash hash(QCryptographicHash::Sha256);validPdf=f.open(QIODevice::ReadOnly)&&hash.addData(&f)&&hash.result().toHex()==m_pdfHash.toLatin1();}
        for(auto const& e:m_entries){
            if(!removable(e,event))continue;
            if(e.kind=="native"&&!e.path.endsWith(".ocr.tif",Qt::CaseInsensitive)&&!validPdf){++failed;continue;}
            QFileInfo current(e.path);
            if(!owns(e.root)||!contained(e.path,e.root)||current.size()!=e.bytes||current.lastModified().toMSecsSinceEpoch()!=e.modified){++failed;continue;}
            if(QFile::remove(e.path)){removed+=e.bytes;++count;}else ++failed;
        }
        // Empty subfolders only, followed by empty owned roots with only markers.
        for(auto value:m_registry.value("roots").toArray()){
            auto root=value.toObject().value("path").toString();if(!owns(root))continue;
            QStringList empty;QDirIterator it(root,QDir::Dirs|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);while(it.hasNext()){auto p=it.next();if(!QFileInfo(p).isSymLink()&&QFileInfo(p).canonicalFilePath().startsWith(QFileInfo(root).canonicalFilePath()+"/",pathCase()))empty<<p;}
            std::sort(empty.begin(),empty.end(),[](QString const& a,QString const& b){return a.size()>b.size();});for(auto const& p:empty)QDir().rmdir(p);
        }
        for(auto value:m_registry.value("roots").toArray()){
            auto row=value.toObject();auto root=row.value("path").toString(),role=row.value("role").toString();
            if(role=="native"||!owns(root))continue;
            auto names=QDir(root).entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot);bool empty=true;
            for(auto const& name:names)if(name!=".stou-cache-owner.json"&&name!=".stou-owner.json"&&name!=".stou-import-session.json")empty=false;
            if(empty){for(auto const& name:names)QFile::remove(root+"/"+name);QDir().rmdir(root);}
        }
        scan();m_status->setText(text("Silindi: ","Removed: ")+sizeText(removed)+" · "+QString::number(count)+text(" dosya. Korunan/kullanımdaki: "," files. Protected/in use: ")+QString::number(failed)+(validPdf?QString():text(" · TIFF temizliği için doğrulanmış PDF gerekli."," · Verified PDF required to remove TIFFs.")));
        return {{"bytes",double(removed)},{"files",count},{"skipped",failed}};
    }
};
