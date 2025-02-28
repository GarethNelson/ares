/*
The MIT License

Copyright (c) 2012 by Jorrit Tyberghein

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
 */

#ifndef __iapp_h__
#define __iapp_h__

#include "csutil/scf.h"
#include <wx/wx.h>

struct iUIManager;
struct iEditorConfig;
struct i3DView;
struct iAssetManager;
struct iObject;
struct iDynamicObject;
struct iCameraWindow;

/**
 * The editor application.
 */
struct iAresEditor : public virtual iBase
{
  SCF_INTERFACE(iAresEditor,0,0,1);

  virtual iObjectRegistry* GetObjectRegistry () const = 0;
  virtual iUIManager* GetUI () const = 0;
  virtual i3DView* Get3DView () const = 0;
  virtual iAssetManager* GetAssetManager () const = 0;
  virtual iCameraWindow* GetCameraWindow () const = 0;

  virtual bool ReportError (const char* description, ...) = 0;

  /**
   * Register that a resource has been modified.
   * If the resource is not given then it just registers a general modification.
   */
  virtual void RegisterModification (iObject* resource = 0) = 0;

  /**
   * Register that a series of resources has been modified.
   * This version will only ask for a new asset once and use that
   * for all subsequent resources.
   */
  virtual void RegisterModification (const csArray<iObject*>& resources) = 0;

  /**
   * Set the focus to the 3d view so that keyboard commands work correctly.
   */
  virtual void SetFocus3D () = 0;

  /// Set the state of the menus correctly depending on context.
  virtual void SetMenuState () = 0;

  /// Set the state of a toggle menu/toolbar to a specific value.
  virtual void SetMenuItemState (const char* command, bool checked) = 0;

  /**
   * Update the title of this frame. Useful to call after making a modification to
   * make sure the '*' is added.
   */
  virtual void UpdateTitle () = 0;

  /// After changing config settings, let the application read the config again.
  virtual void ReadConfig () = 0;

  /// Set the help status message at the bottom of the frame.
  virtual void SetStatus (const char* statusmsg, ...) = 0;
  /// Clear the help status message (go back to default).
  virtual void ClearStatus () = 0;

  /// Show the camera window. @@@ Temporary!
  virtual void ShowCameraWindow () = 0;
  /// Hide the camera window. @@@ Temporary!
  virtual void HideCameraWindow () = 0;

  /// Get the editor configuration object.
  virtual iEditorConfig* GetConfig () const = 0;

  /**
   * Switch to the main default mode.
   */
  virtual void SwitchToMainMode () = 0;

  /**
   * Set the current object comment.
   */
  virtual void SetObjectForComment (const char* type, iObject* objForComment) = 0;

  /**
   * Open the editor for a given resource.
   */
  virtual void OpenEditor (iObject* resource) = 0;
  virtual void OpenEditor (iDynamicObject* object) = 0;
};


#endif // __iapp_h__

