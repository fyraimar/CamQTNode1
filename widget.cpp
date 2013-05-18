#include "widget.h"
#include "ui_widget.h"

Widget::Widget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Widget)
{
    ui->setupUi(this);
    vt = new VideoThread;
    //this->setCursor(Qt::BlankCursor);
}

Widget::~Widget()
{
    delete ui;
}


void Widget::on_oneButton_clicked()
{
    if (vt->isRunning()) {
        vt->stop();
        ui->oneButton->setText(tr("Start"));
    } else {
        ui->oneButton->setText(tr("Pause"));
        vt->start();
    }
}
