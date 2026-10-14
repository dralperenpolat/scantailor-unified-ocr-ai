// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QTranslator>
#include <QVariant>
#include <vector>
#include <algorithm>

namespace unified_i18n {

inline QString selectedLanguage() {
    const auto forced=qEnvironmentVariable("STOU_UI_LANGUAGE");
    if(forced=="en"||forced=="tr")return forced;
    const auto saved=QSettings().value("unified/ui_language").toString();
    if(saved=="en"||saved=="tr")return saved;
    return QLocale::system().language()==QLocale::Turkish?"tr":"en";
}

inline QString text(char const* source) {
    return QCoreApplication::translate("Unified",source);
}
// Decoded process output and paths remain data. Only explicitly selected
// presentation fields go through display(); queue records stay unchanged.
inline QString text(QByteArray const& bytes) { return QString::fromUtf8(bytes); }
inline QString text(char const* bytes,int size) { return QString::fromUtf8(bytes,size); }
inline QString display(QString const& value) { return text(value.toUtf8().constData()); }

class EnglishTranslator final:public QTranslator {
    struct Pattern { QRegularExpression expression;QString translation;QStringList parameters;int specificity=0; };
    QHash<QString,QString> m_messages;
    std::vector<Pattern> m_patterns;
public:
    explicit EnglishTranslator(QObject* parent=nullptr):QTranslator(parent) {
        QFile file(":/i18n/unified-en.json");
        if(!file.open(QIODevice::ReadOnly))return;
        const auto messages=QJsonDocument::fromJson(file.readAll()).object();
        const QRegularExpression parameter("%[1-9][0-9]*");
        for(auto it=messages.begin();it!=messages.end();++it) {
            const auto source=it.key(),target=it.value().toString();
            if(target.isEmpty())continue;
            m_messages.insert(source,target);
            if(source==target||!source.contains(parameter))continue;
            Pattern p;p.translation=target;p.specificity=source.size();QString pattern="^";int offset=0;
            auto matches=parameter.globalMatch(source);
            while(matches.hasNext()) {
                const auto match=matches.next();
                pattern+=QRegularExpression::escape(source.mid(offset,match.capturedStart()-offset));
                const auto key=match.captured();const int previous=p.parameters.indexOf(key);
                if(previous>=0)pattern+="\\g{"+QString::number(previous+1)+"}";
                else {pattern+="(.*?)";p.parameters<<key;}
                offset=match.capturedEnd();
            }
            pattern+=QRegularExpression::escape(source.mid(offset))+"$";
            p.expression=QRegularExpression(pattern,QRegularExpression::DotMatchesEverythingOption);
            if(p.expression.isValid())m_patterns.push_back(p);
        }
        std::stable_sort(m_patterns.begin(),m_patterns.end(),[](Pattern const& a,Pattern const& b){return a.specificity>b.specificity;});
    }
    bool isEmpty()const override { return m_messages.isEmpty(); }
    QString translate(char const* context,char const* source,char const* =nullptr,int =-1)const override {
        if(!context||QByteArray(context)!="Unified"||!source)return {};
        const auto value=QString::fromUtf8(source);
        const auto found=m_messages.constFind(value);
        if(found!=m_messages.constEnd())return found.value();
        // Worker messages may contain formatted counters or filenames. Match
        // complete known templates, never substrings inside a user's data.
        if(value.size()>4096)return {};
        const QRegularExpression parameter("%[1-9][0-9]*");
        for(auto const& p:m_patterns) {
            const auto match=p.expression.match(value);if(!match.hasMatch())continue;
            QString result;int offset=0;auto tokens=parameter.globalMatch(p.translation);
            while(tokens.hasNext()) {
                const auto token=tokens.next();
                result+=p.translation.mid(offset,token.capturedStart()-offset);
                const int index=p.parameters.indexOf(token.captured());
                result+=index<0?token.captured():match.captured(index+1);
                offset=token.capturedEnd();
            }
            return result+p.translation.mid(offset);
        }
        return {};
    }
};

} // namespace unified_i18n
