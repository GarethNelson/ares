// This file is automatically generated.
#include "cssysdef.h"
#include "csutil/scf.h"

// Put static linking stuff into own section.
// The idea is that this allows the section to be swapped out but not
// swapped in again b/c something else in it was needed.
#if !defined(CS_DEBUG) && defined(CS_COMPILER_MSVC)
#pragma const_seg(".CSmetai")
#pragma comment(linker, "/section:.CSmetai,r")
#pragma code_seg(".CSmeta")
#pragma comment(linker, "/section:.CSmeta,er")
#pragma comment(linker, "/merge:.CSmetai=.CSmeta")
#endif

namespace csStaticPluginInit
{
static char const metainfo_assetmanager[] =
"<?xml version=\"1.0\"?>"
"<!-- assetmanager.csplugin -->"
"<plugin>"
"  <scf>"
"    <classes>"
"      <class>"
"        <name>utility.assetmanager</name>"
"        <implementation>AssetManager</implementation>"
"        <description>Asset Manager Plugin</description>"
"      </class>"
"    </classes>"
"  </scf>"
"</plugin>"
;
  #ifndef AssetManager_FACTORY_REGISTER_DEFINED 
  #define AssetManager_FACTORY_REGISTER_DEFINED 
    SCF_DEFINE_FACTORY_FUNC_REGISTRATION(AssetManager) 
  #endif

class assetmanager
{
SCF_REGISTER_STATIC_LIBRARY(assetmanager,metainfo_assetmanager)
  #ifndef AssetManager_FACTORY_REGISTERED 
  #define AssetManager_FACTORY_REGISTERED 
    AssetManager_StaticInit AssetManager_static_init__; 
  #endif
public:
 assetmanager();
};
assetmanager::assetmanager() {}

}
