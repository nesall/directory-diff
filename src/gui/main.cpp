#include <QApplication>
#include <QThreadPool>
#include "qmainwindow.h"

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  app.setWindowIcon(QIcon(":/main.ico"));
  app.setOrganizationName("CrestCapitalGLOW");
  app.setOrganizationDomain("CrestCapitalGLOW.com");

  int result;
  {
    QMainWindowDiff window;
    window.resize(1200, 800);
    window.show();
    result = app.exec();
  }
  // Closing the window requests cancellation. Let workers release staged files before Qt shuts down.
  QThreadPool::globalInstance()->waitForDone();
  return result;
}
