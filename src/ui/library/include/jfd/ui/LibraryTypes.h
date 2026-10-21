#pragma once

#include <jfd/library/HomeModel.h>
#include <jfd/library/ItemDetails.h>
#include <jfd/library/ItemListModel.h>
#include <jfd/library/LibraryModel.h>
#include <jfd/library/SearchModel.h>
#include <jfd/library/SignIn.h>

#include <QObject>
#include <qqmlintegration.h>

// The library module's models as QML types of Jfd.Ui.Library, so screens
// can use their enums (SignIn.Credentials, LibraryModel.Unplayed) and
// tooling knows their properties. Instances come from `Library`.
namespace jfd::ui {

  struct SignInForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::SignIn)
    QML_NAMED_ELEMENT(SignIn)
    QML_UNCREATABLE("Use Library.signIn")
  };

  struct HomeModelForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::HomeModel)
    QML_NAMED_ELEMENT(HomeModel)
    QML_UNCREATABLE("Use Library.home")
  };

  struct LibraryModelForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::LibraryModel)
    QML_NAMED_ELEMENT(LibraryModel)
    QML_UNCREATABLE("Use Library.createLibrary()")
  };

  struct ItemDetailsForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::ItemDetails)
    QML_NAMED_ELEMENT(ItemDetails)
    QML_UNCREATABLE("Use Library.createDetails()")
  };

  struct SearchModelForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::SearchModel)
    QML_NAMED_ELEMENT(SearchModel)
    QML_UNCREATABLE("Use Library.search")
  };

  struct ItemListModelForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::ItemListModel)
    QML_NAMED_ELEMENT(ItemListModel)
    QML_UNCREATABLE("Part of the other models")
  };

} // namespace jfd::ui
