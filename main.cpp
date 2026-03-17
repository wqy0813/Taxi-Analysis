#include <QApplication>
 #include <QMainWindow>
 #include <QPushButton>
 #include <QVBoxLayout>
 #include <QWidget>
 
 int main(int argc, char *argv[])
 {
     QApplication app(argc, argv);
     
     QMainWindow window;
     window.setWindowTitle("交通分析系统");
     window.resize(400, 500);
     
     QWidget *centralWidget = new QWidget(&window);
     QVBoxLayout *layout = new QVBoxLayout(centralWidget);
     
     QPushButton *btn1 = new QPushButton("查询轨迹", centralWidget);
     QPushButton *btn2 = new QPushButton("区域查找", centralWidget);
     QPushButton *btn3 = new QPushButton("车辆密度", centralWidget);
     QPushButton *btn4 = new QPushButton("区域关联分析", centralWidget);
     QPushButton *btn5 = new QPushButton("频繁路径分析", centralWidget);
     QPushButton *btn6 = new QPushButton("通行时间分析", centralWidget);
     
     layout->addWidget(btn1);
     layout->addWidget(btn2);
     layout->addWidget(btn3);
     layout->addWidget(btn4);
     layout->addWidget(btn5);
     layout->addWidget(btn6);
     
     layout->setSpacing(10);
     layout->setContentsMargins(20, 20, 20, 20);
     
     window.setCentralWidget(centralWidget);
     window.show();
     
     return app.exec();
 }