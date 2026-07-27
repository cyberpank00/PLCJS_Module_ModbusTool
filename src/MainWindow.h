#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

// Single-window application with three tabs: Онлайн, Настройки, Обновление FW.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
};

#endif // MAINWINDOW_H
