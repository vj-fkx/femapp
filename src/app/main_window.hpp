#pragma once

#include <QMainWindow>

namespace fem::app {

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void showAbout();
};

} // namespace fem::app
