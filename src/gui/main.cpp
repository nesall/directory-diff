#include <QApplication>
#include "qmainwindow.h"

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  app.setWindowIcon(QIcon(":/main.ico"));
  app.setOrganizationName("CrestCapitalGLOW");
  app.setOrganizationDomain("CrestCapitalGLOW.com");

  QMainWindowDiff window;
  window.resize(1200, 800);
  window.show();

  return app.exec();
}