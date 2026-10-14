// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QScrollArea>
#include <QComboBox>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QSlider>
#include <QEvent>
#include <QCoreApplication>
#include <QVariant>

// A wheel over a closed drop-down scrolls the settings panel. Numeric controls
// accept wheel changes only after explicit keyboard/click focus.
class UnifiedWheelGuard : public QObject {
public:
    explicit UnifiedWheelGuard(QScrollArea* area):QObject(area),m_area(area) {}
protected:
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()!=QEvent::Wheel)return false;
        auto widget=qobject_cast<QWidget*>(object);
        if(!widget)return false;
        if(auto combo=qobject_cast<QComboBox*>(widget)) {
            if(combo->view()->isVisible())return false;
        } else if(widget->hasFocus())return false;
        QCoreApplication::sendEvent(m_area->viewport(),event);
        event->accept();return true;
    }
private:
    QScrollArea* m_area;
};

inline void installUnifiedWheelGuard(QScrollArea* area,QWidget* content) {
    if(content->property("unifiedWheelGuarded").toBool())return;
    content->setProperty("unifiedWheelGuarded",true);
    auto guard=new UnifiedWheelGuard(area);
    for(auto widget:content->findChildren<QWidget*>()) {
        if(qobject_cast<QComboBox*>(widget)||qobject_cast<QAbstractSpinBox*>(widget)||qobject_cast<QSlider*>(widget)) {
            widget->setFocusPolicy(Qt::StrongFocus);
            widget->installEventFilter(guard);
        }
    }
}
