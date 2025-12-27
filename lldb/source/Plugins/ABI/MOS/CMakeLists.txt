add_lldb_library(lldbPluginABIMOS PLUGIN
  ABISysV_mos.cpp

  LINK_COMPONENTS
    Support
    TargetParser
  LINK_LIBS
    lldbCore
    lldbSymbol
    lldbTarget
    lldbValueObject
  )
