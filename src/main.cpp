#include "database.hh"
#include "gui.hh"

#include <QApplication>

int
main(int argc, char* argv[])
{
  QApplication app(argc, argv);

  Database db("../data/portfolio.db");
  PortfolioWidget widget(&db);
  widget.show();

  return app.exec();
}
