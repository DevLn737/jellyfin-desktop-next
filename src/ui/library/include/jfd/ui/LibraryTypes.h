#pragma once

#include <jfd/library/HomeModel.h>
#include <jfd/library/ItemDetails.h>
#include <jfd/library/ItemListModel.h>
#include <jfd/library/ItemRangeModel.h>
#include <jfd/library/LibraryModel.h>
#include <jfd/library/LiveTvChannels.h>
#include <jfd/library/LiveTvGuide.h>
#include <jfd/library/LiveTvRecorder.h>
#include <jfd/library/LiveTvRecordings.h>
#include <jfd/library/LiveTvSchedule.h>
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

  // A window of another model's rows (a long season's range of episodes).
  struct ItemRangeModelForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::ItemRangeModel)
    QML_NAMED_ELEMENT(ItemRangeModel)
  };

  struct LiveTvChannelsForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::LiveTvChannels)
    QML_NAMED_ELEMENT(LiveTvChannels)
    QML_UNCREATABLE("Use Library.liveTv")
  };

  struct LiveTvGuideForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::LiveTvGuide)
    QML_NAMED_ELEMENT(LiveTvGuide)
    QML_UNCREATABLE("Use Library.createGuide()")
  };

  struct LiveTvScheduleForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::LiveTvSchedule)
    QML_NAMED_ELEMENT(LiveTvSchedule)
    QML_UNCREATABLE("Use Library.createSchedule()")
  };

  struct LiveTvRecorderForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::LiveTvRecorder)
    QML_NAMED_ELEMENT(LiveTvRecorder)
    QML_UNCREATABLE("Use Library.recorder")
  };

  struct LiveTvRecordingsForeign {
    Q_GADGET
    QML_FOREIGN(jfd::library::LiveTvRecordings)
    QML_NAMED_ELEMENT(LiveTvRecordings)
    QML_UNCREATABLE("Use Library.createRecordings()")
  };

} // namespace jfd::ui
