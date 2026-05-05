#pragma once

#include <QObject>
#include <qmmpui/general.h>
#include <qmmpui/generalfactory.h>

class DiscordRichPresenceFactory : public QObject, public GeneralFactory
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qmmp.qmmpui.GeneralFactoryInterface.1.0")
    Q_INTERFACES(GeneralFactory)
public:
    GeneralProperties properties() const override;
    QObject *create(QObject *parent) override;
    QDialog *createSettings(QWidget *parent) override;
    void showAbout(QWidget *parent) override;
    QString translation() const override;
};
