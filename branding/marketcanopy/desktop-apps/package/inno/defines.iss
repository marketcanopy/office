; -- Market Canopy Office installer defines --
;
; Copy of desktop-apps package/inno/defines.iss (ONLYOFFICE, Copyright (C)
; Ascensio System SIA) at the tag in UPSTREAM. Modified by Market Canopy on
; 2026-10-24: product, publisher, registry, protocol, window, mutex, executable
; and ProgID names (P02 contract section 2). The extension lists below are
; upstream's, unchanged. AGPL-3.0-only with the additional terms in LICENSE.

#define sCompanyName                    "Market Canopy Office"
#define sIntCompanyName                 "MarketCanopy"
#define sProductName                    "Office"
#define sIntProductName                 "Office"
#define sAppName                        "Market Canopy Office"
#define sPackageName                    str(sIntCompanyName + "-" + sIntProductName)
#define sAppPublisher                   "Market Canopy LLC"
#define sAppPublisherURL                "https://marketcanopy.com/"
#define sAppSupportURL                  "https://marketcanopy.com/help"
#define sAppCopyright                   str("Copyright (C) Ascensio System SIA 2009-" + GetDateTimeString("yyyy",,) + ". Modifications copyright (C) Market Canopy LLC 2026-" + GetDateTimeString("yyyy",,) + ".")
#define sAppIconName                    "Market Canopy Office"
; Upstream deletes shortcuts its older versions left; ours has no older version,
; and must never delete an ONLYOFFICE shortcut, so the name is our own.
#define sOldAppIconName                 "Market Canopy Office (previous)"
#define sAppProtocol                    'mc-office'

#define APP_PATH                        str(sIntCompanyName + "\" + sIntProductName)
#define UPD_PATH                        str(sIntProductName + "Updates")
#define APP_REG_PATH                    str("Software\" + APP_PATH)
#define APP_REG_UNINST_KEY              "Market Canopy Office"
#define APP_USER_MODEL_ID               "MarketCanopy.Office"
#define APP_MUTEX_NAME                  "MarketCanopyOffice"
#define APPWND_CLASS_NAME               "MarketCanopyOfficeWindowClass"

#define iconsExe                        "MarketCanopyOffice.exe"
#define NAME_EXE_OUT                    "editors.exe"

#define ASSC_APP_NAME                   "Market Canopy Office"
; ProgIDs, Software\RegisteredApplications and the protocol's ProgID are all
; distinct from ONLYOFFICE's, so installing or removing Office never touches an
; ONLYOFFICE Desktop Editors installation on the same machine.
#define ASCC_REG_PREFIX                 "MarketCanopy.Office"
#define ASCC_REG_REGISTERED_APP_NAME    "Market Canopy Office"
#define ASSOC_PROG_ID                   "MarketCanopy.Office.Editors"
#define ASSOC_APP_FRIENDLY_NAME         "Market Canopy Office"

#dim FA_ARR[61] { \
  "csv", \
  "djvu", \
  "doc", \
  "docm", \
  "docx", \
  "dot", \
  "dotm", \
  "dotx", \
  "dps", \
  "dpt", \
  "epub", \
  "fb2", \
  "fodp", \
  "fods", \
  "fodt", \
  "hwp", \
  "hwpx", \
  "key", \
  "md", \
  "numbers", \
  "odg", \
  "odp", \
  "ods", \
  "odt", \
  "otp", \
  "ots", \
  "ott", \
  "oxps", \
  "pages", \
  "pdf", \
  "pot", \
  "potm", \
  "potx", \
  "pps", \
  "ppsm", \
  "ppsx", \
  "ppt", \
  "pptm", \
  "pptx", \
  "rtf", \
  "stw", \
  "sxc", \
  "sxi", \
  "sxw", \
  "tsv", \
  "vsdm", \
  "vsdx", \
  "vssm", \
  "vssx", \
  "vstm", \
  "vstx", \
  "wps", \
  "wpt", \
  "xls", \
  "xlsb", \
  "xlsm", \
  "xlsx", \
  "xlt", \
  "xltm", \
  "xltx", \
  "xps" \
}

#dim SN_ARR[4] { \
  "docx", \
  "xlsx", \
  "pptx", \
  "pdf" \
}
