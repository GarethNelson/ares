/*
The MIT License

Copyright (c) 2010 by Jorrit Tyberghein

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

#include "appares.h"
#ifdef USE_CEL
#include <celtool/initapp.h>
#endif
#include <cstool/simplestaticlighter.h>
#include <csgeom/math3d.h>
#include "camerawin.h"

// Convenience function to get a csVector3 from a config file.
static csVector3 GetConfigVector (iConfigFile* config,
        const char* key, const char* def)
{
  csVector3 v;
  csScanStr (config->GetStr (key, def), "%f,%f,%f", &v.x, &v.y, &v.z);
  return v;
}

AppAresEdit::AppAresEdit() : csApplicationFramework()
{
  SetApplicationName("ares");
  do_debug = false;
  do_simulation = true;
  filereq = 0;
  camwin = 0;
  sun_alfa = 3.21f;
  sun_theta = 0.206f;
  min_light = 0.0f;
  currentTime = 0;
  do_auto_time = false;
  editMode = 0;
  mainMode = 0;
  curveMode = 0;
  do_panning = false;
  curvedFactoryCounter = 0;
}

AppAresEdit::~AppAresEdit()
{
  delete filereq;
  delete camwin;
  delete mainMode;
  delete curveMode;
}

void AppAresEdit::UpdateTime (csTicks ticks)
{
  iCamera* cam = view->GetCamera ();

  static float lastStep = float (ticks % 100000) / 100000.0;
  float step = float (ticks % 100000) / 100000.0;

  // Don't update if the time has not changed much.
  if ((step - lastStep) < 0.0001f && (step - lastStep) > -0.0001f) return;
  lastStep = step;

  //=[ Sun position ]===================================
  //TODO: Make the sun stay longer at its highest point at noon.
  float temp = step * 2.0f;
  if (temp > 1.0f) temp  = 2.0f - temp;

  sun_theta = (2.0f*temp - 1.0f)*0.85;
  sun_alfa = 1.605f * sin(-step * 2.0f*PI) - 3.21f;

  // Update the values.
  csVector3 sun_vec;
  sun_vec.x = cos(sun_theta)*sin(sun_alfa);
  sun_vec.y = sin(sun_theta);
  //if (sun_vec.y <= 0) sun_vec.y = 0;
  sun_vec.z = cos(sun_theta)*cos(sun_alfa);
  csShaderVariable* var = shaderMgr->GetVariableAdd(string_sunDirection);
  var->SetValue(sun_vec);

  // Set the sun position.
  csReversibleTransform trans(csMatrix3(), (sun_vec*1000.0f)+cam->GetTransform().GetOrigin());
  trans.LookAt (sun_vec*-1, csVector3(0,1,0));
  sun->GetMovable()->SetTransform (trans);
  sun->GetMovable()->UpdateMove();

  //=[ Sun brightness ]===================================
  // This is just "Lambert's cosine law" shifted so midday is 0, and
  // multiplied by 1.9 instead of 2 to extend the daylight after sunset to
  float brightness = cos((step - 0.5f) * PI * 1.9f);
  //csColor sunlight(brightness * 1.5f);
  csColor sunlight(brightness);
  sunlight.ClampDown();
  // The ambient color is adjusted to give a slightly more yellow colour at
  // midday, graduating to a purplish blue at midnight. Adjust "min_light"
  // in options to make it playable at night.
  float amb = cos((step - 0.5f) * PI * 2.2f);
  csColor ambient((amb*0.125f)+0.075f+min_light, (amb*0.15f)+0.05f+min_light,
        (amb*0.1f)+0.08f+min_light);
  ambient.ClampDown();

  // Update the values.
  sector->SetDynamicAmbientLight(ambient);
  sun->SetColor(sunlight);

  //=[ Clouds ]========================================
  //<shadervar type="vector3" name="cloudcol">0.98,0.59,0.46</shadervar>
  float brightnessc = (amb * 0.6f) + 0.4f;
  CS::ShaderVarStringID time = strings->Request("timeOfDay");
  csRef<csShaderVariable> sv = shaderMgr->GetVariableAdd(time);
  sv->SetValue(brightnessc);
}

void AppAresEdit::DoStuffOncePerFrame()
{
  /* Note: this code is taken from simpmap tutorial.
           /c/System9/proj/CrystalSpaceLibs/common/include/elements/CEGUICheckbox.h:Remove or customize it !
  */
  // First get elapsed time from the virtual clock.
  csTicks elapsed_time = vc->GetElapsedTicks ();
  UpdateTime (currentTime);
  if (do_auto_time)
    currentTime += elapsed_time;
  // speed is a "magic value" which can help with FPS independence
  //float speed = (elapsed_time / 1000.0) * (0.03 * 20);

  csVector3 obj_move (0);
  csVector3 obj_rotate (0);
  bool slow = kbd->GetKeyState (CSKEY_CTRL);

  if (kbd->GetKeyState (CSKEY_SHIFT))
  {
    // If the user is holding down shift, the arrow keys will cause
    // the camera to strafe up, down, left or right from it's
    // current position.
    if (kbd->GetKeyState ('d'))
      obj_move = CS_VEC_RIGHT * 3.0f;
    if (kbd->GetKeyState ('a'))
      obj_move = CS_VEC_LEFT * 3.0f;
    if (kbd->GetKeyState ('w'))
      obj_move = CS_VEC_UP * 3.0f;
    if (kbd->GetKeyState ('s'))
      obj_move = CS_VEC_DOWN * 3.0f;
  }
  else
  { 
    // left and right cause the camera to rotate on the global Y
    // axis; page up and page down cause the camera to rotate on the
    // _camera's_ X axis (more on this in a second) and up and down
    // arrows cause the camera to go forwards and backwards.
    if (kbd->GetKeyState ('d'))
      obj_rotate.Set (0, 1, 0);
    if (kbd->GetKeyState ('a'))
      obj_rotate.Set (0, -1, 0);
    if (kbd->GetKeyState (CSKEY_PGUP))
      obj_rotate.Set (1, 0, 0);
    if (kbd->GetKeyState (CSKEY_PGDN))
      obj_rotate.Set (-1, 0, 0);
    if (kbd->GetKeyState ('w'))
      obj_move = CS_VEC_FORWARD * 3.0f;
    if (kbd->GetKeyState ('s'))
      obj_move = CS_VEC_BACKWARD * 3.0f;
  } 

  const float speed = elapsed_time / 1000.0;

  collider_actor.Move (speed, slow ? 0.5f : 2.0f, obj_move, obj_rotate);

  // To get the camera/actor position, you can use that:
  //iCamera* cam = view->GetCamera ();
  //csVector3 pos (cam->GetTransform ().GetOrigin ());
  iCamera* camera = view->GetCamera ();

  csReversibleTransform tc = camera->GetTransform ();
  //csVector3 pos = tc.GetOrigin () + tc.GetT2O () * csVector3 (0, 0, .5);
  csVector3 pos = tc.GetOrigin () + tc.GetT2O () * csVector3 (2, 0, 2);
  camlight->GetMovable ()->GetTransform ().SetOrigin (pos);
  camlight->GetMovable ()->UpdateMove ();

  editMode->FramePre ();

  if (do_simulation)
  {
    float dynamicSpeed = 1.0f;
    dyn->Step (speed / dynamicSpeed);
  }

  dynworld->PrepareView (view->GetCamera (), elapsed_time);
}

void AppAresEdit::Frame()
{
  /*
    Note: if you use CEL, you probably don't want to call DoStuffOncePerFrame()
          nor g3d->BeginDraw() and use the entity/propclass system.
          This part (similar to simpmap tutorial) is just there as a kickstart.
  */

  DoStuffOncePerFrame ();

  if (g3d->BeginDraw(engine->GetBeginDrawFlags() | CSDRAW_3DGRAPHICS))
  {
    // Draw frame.
#ifdef USE_CEL
    // When using CEL, the entity system takes care of drawing,
    // so there's no need to call iView::Draw() here.
    // That template doesn't setup any CEL entity, so we have to call it now.
    // Once you have your entities set, remove it.
    view->Draw ();
#else
    view->Draw ();
#endif
  }

  editMode->Frame3D ();

  if (do_debug)
    bullet_dynSys->DebugDraw (view);

  g3d->BeginDraw (CSDRAW_2DGRAPHICS);
  csString buf;
  const csOrthoTransform& trans = view->GetCamera ()->GetTransform ();
  const csVector3& origin = trans.GetOrigin ();
  buf.Format ("%g,%g,%g", origin.x, origin.y, origin.z);
  iGraphics2D* g2d = g3d->GetDriver2D ();
  g2d->Write (font, 200, g2d->GetHeight ()-20, colorWhite, -1, buf);

  editMode->Frame2D ();

  cegui->Render ();
}

bool AppAresEdit::OnMouseMove (iEvent& ev)
{
  // Save the mouse position
  mouseX = csMouseEventHelper::GetX (&ev);
  mouseY = csMouseEventHelper::GetY (&ev);
  return editMode->OnMouseMove(ev, mouseX, mouseY);

  return false;
}

void AppAresEdit::SetStaticSelectedObjects (bool st)
{
  csArray<iDynamicObject*>::Iterator it = current_objects.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicObject* dynobj = it.Next ();
    if (st)
    {
      if (!dynobj->IsStatic ())
        dynobj->MakeStatic ();
    }
    else
    {
      if (dynobj->IsStatic ())
      {
        PushUndo ("Static");
        dynobj->MakeDynamic ();
      }
    }
  }
}

void AppAresEdit::SetButtonState ()
{
  bool curveTabEnable = false;
  if (current_objects.GetSize () == 1)
  {
    csString name = current_objects[0]->GetFactory ()->GetName ();
    if (!curvedMeshCreator->GetCurvedFactory (name))
      curveTabEnable = true;
  }
  if (curveTabEnable)
    curveTabButton->enable ();
  else
    curveTabButton->disable ();
}

void AppAresEdit::AddCurrentObject (iDynamicObject* dynobj)
{
  if (!dynobj) return;
  if (current_objects.Find (dynobj) != csArrayItemNotFound)
  {
    current_objects.Delete (dynobj);
    dynobj->SetHilight (false);
  }
  else
  {
    current_objects.Push (dynobj);
    dynobj->SetHilight (true);
  }
  mainMode->CurrentObjectsChanged (current_objects);
}

void AppAresEdit::SetCurrentObject (iDynamicObject* dynobj)
{
  if (current_objects.Find (dynobj) != csArrayItemNotFound) return;
  csArray<iDynamicObject*>::Iterator it = current_objects.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicObject* dynobj = it.Next ();
    dynobj->SetHilight (false);
  }
  current_objects.DeleteAll ();
  if (dynobj)
  {
    current_objects.Push (dynobj);
    dynobj->SetHilight (true);
  }
  mainMode->CurrentObjectsChanged (current_objects);
  camwin->CurrentObjectsChanged (current_objects);
}

bool AppAresEdit::OnMouseDown (iEvent& ev)
{
  uint but = csMouseEventHelper::GetButton (&ev);
  mouseX = csMouseEventHelper::GetX (&ev);
  mouseY = csMouseEventHelper::GetY (&ev);

  return editMode->OnMouseDown (ev, but, mouseX, mouseY);
}

bool AppAresEdit::OnMouseUp (iEvent& ev)
{
  uint but = csMouseEventHelper::GetButton (&ev);
  mouseX = csMouseEventHelper::GetX (&ev);
  mouseY = csMouseEventHelper::GetY (&ev);

  return editMode->OnMouseUp (ev, but, mouseX, mouseY);
}

bool AppAresEdit::OnKeyboard(iEvent& ev)
{
  if (csKeyEventHelper::GetEventType(&ev) == csKeyEventTypeDown)
  {
    // The user pressed a key (as opposed to releasing it).
    utf32_char code = csKeyEventHelper::GetCookedCode(&ev);
    if (code == CSKEY_ESC)
    {
      // The user pressed escape, so terminate the application.  The proper way
      // to terminate a Crystal Space application is by broadcasting a
      // csevQuit event.  That will cause the main run loop to stop.  To do
      // so we retrieve the event queue from the object registry and then post
      // the event.
      csRef<iEventQueue> q =
        csQueryRegistry<iEventQueue> (GetObjectRegistry());
      if (q.IsValid())
        q->GetEventOutlet()->Broadcast(csevQuit(GetObjectRegistry()));
      return true;
    }
    else if (code == '1')
    {
      do_debug = !do_debug;
    }
    else if (code == CSKEY_F2)
    {
      currentTime += 500;
    }
    else if (code == CSKEY_F3)
    {
      do_auto_time = !do_auto_time;
    }
    else if (code == 'c')
    {
      if (camwin->IsVisible ())
	camwin->Hide ();
      else
	camwin->Show ();
    }
    else
      return editMode->OnKeyboard (ev, code);
  }
  return false;
}

//---------------------------------------------------------------------------

bool AppAresEdit::InitWindowSystem ()
{
  cegui = csQueryRegistry<iCEGUI> (GetObjectRegistry());
  if (!cegui) return ReportError("Failed to locate CEGUI plugin");

  cegui->Initialize ();

  vfs->ChDir ("/cegui/");

  // Load the ice skin (which uses Falagard skinning system)
  cegui->GetSchemeManagerPtr ()->create("ice.scheme");

  cegui->GetSystemPtr ()->setDefaultMouseCursor("ice", "MouseArrow");

  cegui->GetFontManagerPtr ()->createFreeTypeFont("DejaVuSans", 10, true, "/fonts/ttf/DejaVuSans.ttf");

  CEGUI::WindowManager* winMgr = cegui->GetWindowManagerPtr ();

  // Load layout and set as root
  vfs->ChDir ("/this/data/windows/");
  cegui->GetSystemPtr ()->setGUISheet(winMgr->loadWindowLayout("ice.layout"));

  CEGUI::Window* btn;

  filenameLabel = winMgr->getWindow("Ares/StateWindow/File");

  btn = winMgr->getWindow("Ares/StateWindow/Save");
  btn->subscribeEvent(CEGUI::PushButton::EventClicked,
    CEGUI::Event::Subscriber(&AppAresEdit::OnSaveButtonClicked, this));
  btn = winMgr->getWindow("Ares/StateWindow/Load");
  btn->subscribeEvent(CEGUI::PushButton::EventClicked,
    CEGUI::Event::Subscriber(&AppAresEdit::OnLoadButtonClicked, this));
  undoButton = static_cast<CEGUI::PushButton*>(winMgr->getWindow("Ares/StateWindow/Undo"));
  undoButton->subscribeEvent(CEGUI::PushButton::EventClicked,
    CEGUI::Event::Subscriber(&AppAresEdit::OnUndoButtonClicked, this));
  undoButton->disable ();

  mainTabButton = static_cast<CEGUI::TabButton*>(winMgr->getWindow("Ares/StateWindow/MainTab"));
  mainTabButton->subscribeEvent(CEGUI::PushButton::EventClicked,
    CEGUI::Event::Subscriber(&AppAresEdit::OnMainTabButtonClicked, this));
  mainTabButton->setTargetWindow(winMgr->getWindow("Ares/ItemWindow"));
  curveTabButton = static_cast<CEGUI::TabButton*>(winMgr->getWindow("Ares/StateWindow/CurveTab"));
  curveTabButton->subscribeEvent(CEGUI::PushButton::EventClicked,
    CEGUI::Event::Subscriber(&AppAresEdit::OnCurveTabButtonClicked, this));
  curveTabButton->setTargetWindow(winMgr->getWindow("Ares/CurveWindow"));

  simulationCheck = static_cast<CEGUI::Checkbox*>(winMgr->getWindow("Ares/StateWindow/Simulation"));
  simulationCheck->subscribeEvent(CEGUI::Checkbox::EventCheckStateChanged,
    CEGUI::Event::Subscriber(&AppAresEdit::OnSimulationSelected, this));

  filereq = new FileReq (cegui, vfs, "/saves");
  camwin = new CameraWindow (this, cegui);

  mainMode = new MainMode (this);
  curveMode = new CurveMode (this);
  editMode = mainMode;
  editMode->Start ();
  mainTabButton->setSelected(true);

  return true;
}

bool AppAresEdit::OnSimulationSelected (const CEGUI::EventArgs&)
{
  do_simulation = simulationCheck->isSelected ();
  return true;
}

CamLocation AppAresEdit::GetCameraLocation ()
{
  CamLocation loc;
  loc.pos = view->GetCamera ()->GetTransform ().GetOrigin ();
  loc.rot = collider_actor.GetRotation ();
  return loc;
}

void AppAresEdit::SetCameraLocation (const CamLocation& loc)
{
  view->GetCamera ()->GetTransform ().SetOrigin (loc.pos);
  collider_actor.SetRotation (loc.rot);
}

void AppAresEdit::CamMove (const csVector3& pos)
{
  view->GetCamera ()->GetTransform ().SetOrigin (pos);
}

void AppAresEdit::CamMoveAndLookAt (const csVector3& pos, const csVector3& rot)
{
  view->GetCamera ()->GetTransform ().SetOrigin (pos);
  collider_actor.SetRotation (rot);
}

void AppAresEdit::CamLookAt (const csVector3& rot)
{
  collider_actor.SetRotation (rot);
}

void AppAresEdit::EnableGravity ()
{
  collider_actor.SetGravity (9.806);
}

void AppAresEdit::DisableGravity ()
{
  collider_actor.SetGravity (0);
}

void AppAresEdit::EnablePanning (const csVector3& center)
{
  do_panning = true;
  panningCenter = center;
}

void AppAresEdit::DisablePanning ()
{
  do_panning = false;
}

void AppAresEdit::DeleteSelectedObjects ()
{
  PushUndo ("Del");
  csArray<iDynamicObject*> objects = current_objects;
  SetCurrentObject (0);
  csArray<iDynamicObject*>::Iterator it = objects.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicObject* dynobj = it.Next ();
    dynworld->DeleteObject (dynobj);
  }
}

void AppAresEdit::MoveCurrent (const csVector3& baseVector)
{
  PushUndo ("Move");
  bool slow = kbd->GetKeyState (CSKEY_CTRL);
  bool fast = kbd->GetKeyState (CSKEY_SHIFT);
  csVector3 vector = baseVector;
  if (slow) vector *= .01;
  else if (!fast) vector *= .1;

  csArray<iDynamicObject*>::Iterator it = current_objects.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicObject* dynobj = it.Next ();
    iMeshWrapper* mesh = dynobj->GetMesh ();
    if (!mesh) continue;

    dynobj->MakeKinematic ();
    csReversibleTransform& trans = mesh->GetMovable ()->GetTransform ();
    trans.Translate (trans.This2OtherRelative (vector));
    mesh->GetMovable ()->UpdateMove ();
    dynobj->UndoKinematic ();
  }
}

void AppAresEdit::RotateCurrent (float baseAngle)
{
  PushUndo ("Rot");
  bool slow = kbd->GetKeyState (CSKEY_CTRL);
  bool fast = kbd->GetKeyState (CSKEY_SHIFT);
  float angle = baseAngle;
  if (slow) angle /= 180.0;
  else if (fast) angle /= 2.0;
  else angle /= 8.0;

  csArray<iDynamicObject*>::Iterator it = current_objects.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicObject* dynobj = it.Next ();
    iMeshWrapper* mesh = dynobj->GetMesh ();
    if (!mesh) continue;

    dynobj->MakeKinematic ();
    mesh->GetMovable ()->GetTransform ().RotateOther (csVector3 (0, 1, 0), angle);
    mesh->GetMovable ()->UpdateMove ();
    dynobj->UndoKinematic ();
  }
}

static float GetAngle (float x1, float y1, float x2, float y2)
{
  float dot = x1 * x2 + y1 * y2;
  float angle = acos (dot);
  return angle;
}

static csVector3 FindBiggestHorizontalMovement (
    const csBox3& box1, const csReversibleTransform& trans1,
    const csBox3& box2, const csReversibleTransform& trans2)
{
  // The origin of the second box in the 3D space of the first box.
  csVector3 o2Tr = trans1.Other2This (trans2.GetOrigin ());
  // Transformed bounding box.
  csBox3 box2Tr;
  box2Tr.StartBoundingBox (trans1.Other2ThisRelative (trans2.This2OtherRelative (box2.GetCorner (0))));
  box2Tr.AddBoundingVertexSmart (trans1.Other2ThisRelative (trans2.This2OtherRelative (box2.GetCorner (1))));
  box2Tr.AddBoundingVertexSmart (trans1.Other2ThisRelative (trans2.This2OtherRelative (box2.GetCorner (2))));
  box2Tr.AddBoundingVertexSmart (trans1.Other2ThisRelative (trans2.This2OtherRelative (box2.GetCorner (3))));
  box2Tr.AddBoundingVertexSmart (trans1.Other2ThisRelative (trans2.This2OtherRelative (box2.GetCorner (4))));
  box2Tr.AddBoundingVertexSmart (trans1.Other2ThisRelative (trans2.This2OtherRelative (box2.GetCorner (5))));
  box2Tr.AddBoundingVertexSmart (trans1.Other2ThisRelative (trans2.This2OtherRelative (box2.GetCorner (6))));
  box2Tr.AddBoundingVertexSmart (trans1.Other2ThisRelative (trans2.This2OtherRelative (box2.GetCorner (7))));

  float xr = o2Tr.x - box1.MaxX () + box2Tr.MinX ();
  float xl = box1.MinX () - o2Tr.x - box2Tr.MaxX ();
  float zr = o2Tr.z - box1.MaxZ () + box2Tr.MinZ ();
  float zl = box1.MinZ () - o2Tr.z - box2Tr.MaxZ ();
  csVector3 newpos = trans2.GetOrigin ();
  if (xr >= xl && xr >= zr && xr >= zl)
    newpos = trans1.This2Other (o2Tr + csVector3 (-xr, 0, -o2Tr.z));
  else if (xl >= xr && xl >= zr && xl >= zl)
    newpos = trans1.This2Other (o2Tr + csVector3 (xl, 0, -o2Tr.z));
  else if (zr >= xl && zr >= xr && zr >= zl)
    newpos = trans1.This2Other (o2Tr + csVector3 (-o2Tr.x, 0, -zr));
  else if (zl >= xl && zl >= xr && zl >= zr)
    newpos = trans1.This2Other (o2Tr + csVector3 (-o2Tr.x, 0, zl));
  return newpos;
}

/**
 * Modify the slave transform so that it is nicely aligned to the master
 * transform (horizontally) but with the smallest possible rotation.
 */
static void FindBestAlignedTransform (const csReversibleTransform& masterTrans,
    csReversibleTransform& slaveTrans)
{
  csReversibleTransform masterCopy = masterTrans;
  int i = 4;
  while (i > 0)
  {
    csVector3 front = masterCopy.GetFront ();
    csVector3 fr = slaveTrans.GetFront ();
    float angle = GetAngle (front.x, front.z, fr.x, fr.z);
    if (fabs (angle) <= (M_PI / 4.0))
    {
      slaveTrans.SetO2T (masterCopy.GetO2T ());
      return;
    }
    i--;
    masterCopy.RotateOther (csVector3 (0, 1, 0), M_PI/2.0);
  }
}

void AppAresEdit::AlignSelectedObjects ()
{
  if (current_objects.GetSize () <= 1) return;
  if (!current_objects[0]->GetMesh ()) return;

  PushUndo ("Align");
  const csReversibleTransform& trans = current_objects[0]->GetMesh ()->GetMovable ()->GetTransform ();

  csArray<iDynamicObject*>::Iterator it = current_objects.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicObject* dynobj = it.Next ();
    if (dynobj == current_objects[0]) continue;
    iMeshWrapper* mesh = dynobj->GetMesh ();
    if (!mesh) continue;
    dynobj->MakeKinematic ();
    csReversibleTransform& tr = mesh->GetMovable ()->GetTransform ();
    FindBestAlignedTransform (trans, tr);
    mesh->GetMovable ()->UpdateMove ();
    dynobj->UndoKinematic ();
  }
}

void AppAresEdit::StackSelectedObjects ()
{
  if (current_objects.GetSize () <= 1) return;
  if (!current_objects[0]->GetMesh ()) return;

  PushUndo ("Stack");
  csReversibleTransform firstTrans = current_objects[0]->GetMesh ()->GetMovable ()->GetTransform ();
  csBox3 firstBbox = current_objects[0]->GetFactory ()->GetPhysicsBBox ();

  csArray<iDynamicObject*>::Iterator it = current_objects.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicObject* dynobj = it.Next ();
    if (dynobj == current_objects[0]) continue;
    iMeshWrapper* mesh = dynobj->GetMesh ();
    if (!mesh) continue;
    dynobj->MakeKinematic ();
    const csBox3& bbox = dynobj->GetFactory ()->GetPhysicsBBox ();
    csReversibleTransform& tr = mesh->GetMovable ()->GetTransform ();
    csVector3 v = tr.GetOrigin ();
    v.y = firstTrans.GetOrigin ().y + firstBbox.MaxY () - bbox.MinY ();
    tr.SetOrigin (v);
    mesh->GetMovable ()->UpdateMove ();
    dynobj->UndoKinematic ();

    // Next stack we perform relative to the previous one we stacked.
    firstTrans = tr;
    firstBbox = bbox;
  }
}

void AppAresEdit::SameYSelectedObjects ()
{
  if (current_objects.GetSize () <= 1) return;
  if (!current_objects[0]->GetMesh ()) return;

  PushUndo ("=Y");
  csReversibleTransform trans = current_objects[0]->GetMesh ()->GetMovable ()->GetTransform ();
  float y = trans.GetOrigin ().y;

  csArray<iDynamicObject*>::Iterator it = current_objects.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicObject* dynobj = it.Next ();
    if (dynobj == current_objects[0]) continue;
    iMeshWrapper* mesh = dynobj->GetMesh ();
    if (!mesh) continue;
    dynobj->MakeKinematic ();
    csReversibleTransform& tr = mesh->GetMovable ()->GetTransform ();
    csVector3 v = tr.GetOrigin ();
    v.y = y;
    tr.SetOrigin (v);
    mesh->GetMovable ()->UpdateMove ();
    dynobj->UndoKinematic ();
  }
}

void AppAresEdit::SetPosSelectedObjects ()
{
  if (current_objects.GetSize () <= 1) return;
  if (!current_objects[0]->GetMesh ()) return;

  PushUndo ("SetPos");
  csReversibleTransform firstTrans = current_objects[0]->GetMesh ()->GetMovable ()
    ->GetTransform ();
  csBox3 firstBbox = current_objects[0]->GetFactory ()->GetPhysicsBBox ();

  csArray<iDynamicObject*>::Iterator it = current_objects.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicObject* dynobj = it.Next ();
    if (dynobj == current_objects[0]) continue;
    iMeshWrapper* mesh = dynobj->GetMesh ();
    if (!mesh) continue;

    dynobj->MakeKinematic ();
    csReversibleTransform& tr = mesh->GetMovable ()->GetTransform ();
    const csBox3& bbox = dynobj->GetFactory ()->GetPhysicsBBox ();
    csVector3 newpos = FindBiggestHorizontalMovement (firstBbox, firstTrans, bbox, tr);
    tr.SetOrigin (newpos);
    mesh->GetMovable ()->UpdateMove ();
    dynobj->UndoKinematic ();

    // Next SetPos we perform relative to the previous one we stacked.
    firstTrans = tr;
    firstBbox = bbox;
  }
}

void AppAresEdit::RotResetSelectedObjects ()
{
  PushUndo ("Rot");
  csArray<iDynamicObject*>::Iterator it = current_objects.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicObject* dynobj = it.Next ();
    iMeshWrapper* mesh = dynobj->GetMesh ();
    if (!mesh) continue;

    dynobj->MakeKinematic ();
    mesh->GetMovable ()->GetTransform ().LookAt (csVector3 (0, 0, 1), csVector3 (0, 1, 0));
    mesh->GetMovable ()->UpdateMove ();
    dynobj->UndoKinematic ();
  }
}

void AppAresEdit::PushUndo (const char* type)
{
  if (lastUndoType == type) return;
  csRef<iDocument> xml = SaveDoc ();
  undoStack.Push (xml);
  undoOperations.Push (type);
  while (undoStack.GetSize () > 10)
  {
    undoStack.DeleteIndex (0);
    undoOperations.DeleteIndex (0);
  }
  csString t;
  t.Format ("Undo(%s)", undoOperations[undoOperations.GetSize ()-1]);
  undoButton->setText(CEGUI::String (t.GetData ()));
  undoButton->enable ();
  lastUndoType = type;
}

bool AppAresEdit::OnMainTabButtonClicked (const CEGUI::EventArgs&)
{
  CEGUI::WindowManager* winMgr = cegui->GetWindowManagerPtr ();
  mainTabButton->setSelected(true);
  curveTabButton->setSelected(false);
  winMgr->getWindow("Ares/ItemWindow")->setVisible(true);
  winMgr->getWindow("Ares/CurveWindow")->setVisible(false);
  if (editMode) editMode->Stop ();
  editMode = mainMode;
  editMode->Start ();
  return true;
}

bool AppAresEdit::OnCurveTabButtonClicked (const CEGUI::EventArgs&)
{
  if (current_objects.GetSize () != 1) return true;
  csString name = current_objects[0]->GetFactory ()->GetName ();
  if (!curvedMeshCreator->GetCurvedFactory (name)) return true;

  CEGUI::WindowManager* winMgr = cegui->GetWindowManagerPtr ();
  mainTabButton->setSelected(false);
  curveTabButton->setSelected(true);
  winMgr->getWindow("Ares/ItemWindow")->setVisible(false);
  winMgr->getWindow("Ares/CurveWindow")->setVisible(true);
  if (editMode) editMode->Stop ();
  editMode = curveMode;
  editMode->Start ();
  return true;
}

void AppAresEdit::CleanupWorld ()
{
  SetCurrentObject (0);
  dynworld->DeleteObjects ();
  curvedMeshCreator->DeleteFactories ();

  csArray<iDynamicFactory*>::Iterator it = curvedFactories.GetIterator ();
  while (it.HasNext ())
  {
    iDynamicFactory* dfact = it.Next ();
    static_factories.Delete (dfact->GetName ());
    factory_to_origin_offset.DeleteAll (dfact->GetName ());
    dynworld->RemoveFactory (dfact);
  }
  curvedFactories.DeleteAll ();

}

bool AppAresEdit::OnUndoButtonClicked (const CEGUI::EventArgs&)
{
  if (undoStack.GetSize () <= 0) return true;
  csRef<iDocument> doc = undoStack.Pop ();
  undoOperations.Pop ();
  if (undoStack.GetSize () <= 0)
  {
    undoButton->setText("Undo()");
    undoButton->disable ();
  }
  else
  {
    csString t;
    t.Format ("Undo(%s)", undoOperations[undoOperations.GetSize ()-1]);
    undoButton->setText(CEGUI::String (t.GetData ()));
  }
  LoadDoc (doc);
  lastUndoType = "";
  return true;
}

csRef<iDocument> AppAresEdit::SaveDoc ()
{
  csRef<iDocumentSystem> docsys;
  docsys.AttachNew (new csTinyDocumentSystem ());
  csRef<iDocument> doc = docsys->CreateDocument ();

  csRef<iDocumentNode> root = doc->CreateRoot ();
  csRef<iDocumentNode> rootNode = root->CreateNodeBefore (CS_NODE_ELEMENT);
  rootNode->SetValue ("dynlevel");

  csRef<iDocumentNode> dynworldNode = rootNode->CreateNodeBefore (CS_NODE_ELEMENT);
  dynworldNode->SetValue ("dynworld");
  dynworld->Save (dynworldNode);

  csRef<iDocumentNode> curveNode = rootNode->CreateNodeBefore (CS_NODE_ELEMENT);
  curveNode->SetValue ("curves");
  curvedMeshCreator->Save (curveNode);
  return doc;
}

void AppAresEdit::SaveFile (const char* filename)
{
  filenameLabel->setText (CEGUI::String (filename));
  csRef<iDocument> doc = SaveDoc ();

  csRef<iString> xml;
  xml.AttachNew (new scfString ());
  doc->Write (xml);
  vfs->WriteFile (filename, xml->GetData (), xml->Length ());
}

struct SaveCallback : public OKCallback
{
  AppAresEdit* ares;
  SaveCallback (AppAresEdit* ares) : ares (ares) { }
  virtual void OkPressed (const char* filename)
  {
    ares->SaveFile (filename);
  }
};

bool AppAresEdit::OnSaveButtonClicked (const CEGUI::EventArgs&)
{
  filereq->Show (new SaveCallback (this));
  return true;
}

void AppAresEdit::LoadFile (const char* filename)
{
  csRef<iDocumentSystem> docsys;
  docsys = csQueryRegistry<iDocumentSystem> (object_reg);
  if (!docsys)
    docsys.AttachNew (new csTinyDocumentSystem ());

  csRef<iDocument> doc = docsys->CreateDocument ();
  csRef<iDataBuffer> buf = vfs->ReadFile (filename);
  const char* error = doc->Parse (buf->GetData ());
  if (error)
  {
    printf ("ERROR: %s\n", error); fflush (stdout);
    return;
  }

  PushUndo ("Load");
  LoadDoc (doc);
}

void AppAresEdit::LoadDoc (iDocument* doc)
{
  CleanupWorld ();

  csRef<iDocumentNode> root = doc->GetRoot ();
  csRef<iDocumentNode> dynlevelNode = root->GetNode ("dynlevel");
  csRef<iDocumentNode> curveNode = dynlevelNode->GetNode ("curves");
  if (curveNode)
  {
    csRef<iString> error = curvedMeshCreator->Load (curveNode);
    if (error)
    {
      printf ("ERROR: %s\n", error->GetData ()); fflush (stdout);
      return;
    }
  }

  for (size_t i = 0 ; i < curvedMeshCreator->GetCurvedFactoryCount () ; i++)
  {
    iCurvedFactory* cfact = curvedMeshCreator->GetCurvedFactory (i);
    iDynamicFactory* fact = dynworld->AddFactory (cfact->GetName (), 1.0, -1);
    fact->AddRigidMesh (csVector3 (0), 10.0);
    static_factories.Add (cfact->GetName ());
    curvedFactories.Push (fact);
  }

  csRef<iDocumentNode> dynworldNode = dynlevelNode->GetNode ("dynworld");
  if (dynworldNode)
  {
    csRef<iString> error = dynworld->Load (dynworldNode);
    if (error)
    {
      printf ("ERROR: %s\n", error->GetData ()); fflush (stdout);
      return;
    }
  }
}

struct LoadCallback : public OKCallback
{
  AppAresEdit* ares;
  LoadCallback (AppAresEdit* ares) : ares (ares) { }
  virtual void OkPressed (const char* filename)
  {
    printf ("OkPressed %s\n", filename); fflush (stdout);
    ares->LoadFile (filename);
  }
};

bool AppAresEdit::OnLoadButtonClicked (const CEGUI::EventArgs&)
{
  filereq->Show (new LoadCallback (this));
  return true;
}

bool AppAresEdit::OnInitialize(int argc, char* argv[])
{
  iObjectRegistry* r = GetObjectRegistry();

  // Load application-specific configuration file.
  if (!csInitializer::SetupConfigManager(r,
      "/ares/AppAres.cfg", GetApplicationName()))
    return ReportError("Failed to initialize configuration manager!");

#ifdef USE_CEL
  celInitializer::SetupCelPluginDirs(r);
#endif

  // RequestPlugins() will load all plugins we specify.  In addition it will
  // also check if there are plugins that need to be loaded from the
  // configuration system (both the application configuration and CS or global
  // configurations).  It also supports specifying plugins on the command line
  // via the --plugin= option.
  if (!csInitializer::RequestPlugins(r,
	CS_REQUEST_VFS,
	CS_REQUEST_OPENGL3D,
	CS_REQUEST_ENGINE,
	CS_REQUEST_FONTSERVER,
	CS_REQUEST_IMAGELOADER,
	CS_REQUEST_LEVELLOADER,
	CS_REQUEST_REPORTER,
	CS_REQUEST_REPORTERLISTENER,
	CS_REQUEST_PLUGIN("crystalspace.collisiondetection.opcode", iCollideSystem),
	CS_REQUEST_PLUGIN("crystalspace.dynamics.bullet", iDynamics),
	CS_REQUEST_PLUGIN("crystalspace.cegui.wrapper", iCEGUI),
	CS_REQUEST_PLUGIN("utility.dynamicworld", iDynamicWorld),
	CS_REQUEST_PLUGIN("utility.curvemesh", iCurvedMeshCreator),
	CS_REQUEST_END))
    return ReportError("Failed to initialize plugins!");

  // "Warm up" the event handler so it can interact with the world
  csBaseEventHandler::Initialize(GetObjectRegistry());
 
  return true;
}

void AppAresEdit::OnExit()
{
  printer.Invalidate();
}

void AppAresEdit::AddItem (const char* category, const char* itemname)
{
  if (!categories.In (category))
  {
    categories.Put (category, csStringArray());
    mainMode->AddCategory (category);
  }
  csStringArray a;
  categories.Get (category, a).Push (itemname);
}

bool AppAresEdit::Application()
{
  iObjectRegistry* r = GetObjectRegistry();

  // Open the main system. This will open all the previously loaded plugins
  // (i.e. all windows will be opened).
  if (!OpenApplication(r))
    return ReportError("Error opening system!");

  vfs = csQueryRegistry<iVFS> (r);
  if (!vfs)
    return ReportError("Failed to locate vfs!");

  if (!InitWindowSystem ())
    return false;

  // Set up an event handler for the application.  Crystal Space is fully
  // event-driven.  Everything (except for this initialization) happens in
  // response to an event.
  if (!RegisterQueue (r, csevAllEvents(GetObjectRegistry())))
    return ReportError("Failed to set up event handler!");

  // Now get the pointer to various modules we need.  We fetch them from the
  // object registry.  The RequestPlugins() call we did earlier registered all
  // loaded plugins with the object registry.  It is also possible to load
  // plugins manually on-demand.
  g3d = csQueryRegistry<iGraphics3D> (r);
  if (!g3d)
    return ReportError("Failed to locate 3D renderer!");

  dynworld = csQueryRegistry<iDynamicWorld> (r);
  if (!dynworld)
    return ReportError("Failed to locate dynamic world!");

  curvedMeshCreator = csQueryRegistry<iCurvedMeshCreator> (r);
  if (!curvedMeshCreator)
    return ReportError("Failed to load the curved mesh creator plugin!");

  engine = csQueryRegistry<iEngine> (r);
  if (!engine)
    return ReportError("Failed to locate 3D engine!");
    
  printer.AttachNew(new FramePrinter(GetObjectRegistry()));

  vc = csQueryRegistry<iVirtualClock> (r);
  if (!vc)
    return ReportError ("Failed to locate Virtual Clock!");

  kbd = csQueryRegistry<iKeyboardDriver> (r);
  if (!kbd)
    return ReportError ("Failed to locate Keyboard Driver!");

  loader = csQueryRegistry<iLoader> (r);
  if (!loader)
    return ReportError("Failed to locate map loader plugin!");

  cdsys = csQueryRegistry<iCollideSystem> (r);
  if (!cdsys)
    return ReportError ("Failed to locate CD system!");

  cfgmgr = csQueryRegistry<iConfigManager> (r);
  if (!cfgmgr)
    return ReportError ("Failed to locate the configuration manager plugin!");

  colorWhite = g3d->GetDriver2D ()->FindRGB (255, 255, 255);
  font = g3d->GetDriver2D ()->GetFontServer ()->LoadFont (CSFONT_COURIER);

  // We need a View to the virtual world.
  view.AttachNew(new csView (engine, g3d));
  iGraphics2D* g2d = g3d->GetDriver2D ();
  // We use the full window to draw the world.
  view_width = (int)(g2d->GetWidth () * 0.86);
  view_height = g2d->GetHeight ();
  view->SetRectangle (0, 0, view_width, view_height);

  // Set the window title.
  iNativeWindow* nw = g2d->GetNativeWindow ();
  if (nw)
    nw->SetTitle (cfgmgr->GetStr ("WindowTitle",
          "Please set WindowTitle in AppAresEdit.cfg"));

  if (!InitPhysics ())
    return false;

  if (!CreateWalls ())
    return false;

  dynworld->Setup (sector, dynSys);

  if (!PostLoadMap ())
    return ReportError ("Error during PostLoadMap()!");

  for (size_t i = 0 ; i < dynworld->GetFactoryCount () ; i++)
  {
    iDynamicFactory* fact = dynworld->GetFactory (i);
    printf ("%d %s\n", i, fact->GetName ()); fflush (stdout);
    csBox3 bbox = fact->GetPhysicsBBox ();
    factory_to_origin_offset.Put (fact->GetName (), bbox.MinY ());
    const char* st = fact->GetAttribute ("defaultstatic");
    if (st && *st == 't') static_factories.Add (fact->GetName ());
    const char* category = fact->GetAttribute ("category");
    AddItem (category, fact->GetName ());
  }

  iDynamicFactory* fact = dynworld->AddFactory ("Node", 1.0, -1);
  fact->AddRigidBox (csVector3 (0), csVector3 (.2), 1.0);
  AddItem ("Nodes", "Node");

  for (size_t i = 0 ; i < curvedMeshCreator->GetCurvedFactoryTemplateCount () ; i++)
  {
    iCurvedFactoryTemplate* cft = curvedMeshCreator->GetCurvedFactoryTemplate (i);
    const char* category = cft->GetAttribute ("category");
    AddItem (category, cft->GetName ());

    CurvedFactoryCreator creator;
    creator.name = cft->GetName ();
    const char* maxradiusS = cft->GetAttribute ("maxradius");
    csScanStr (maxradiusS, "%f", &creator.maxradius);
    const char* imposterradiusS = cft->GetAttribute ("imposterradius");
    csScanStr (imposterradiusS, "%f", &creator.imposterradius);
    const char* massS = cft->GetAttribute ("mass");
    csScanStr (massS, "%f", &creator.mass);

    curvedFactoryCreators.Push (creator);
  }

  // Start the default run/event loop.  This will return only when some code,
  // such as OnKeyboard(), has asked the run loop to terminate.
  Run();

  return true;
}

bool AppAresEdit::PostLoadMap ()
{
  // Initialize collision objects for all loaded objects.
  csColliderHelper::InitializeCollisionWrappers (cdsys, engine);

  // Creates an accessor for configuration settings.
  csConfigAccess cfgAcc (GetObjectRegistry ());

  // Find the starting position in this level.
  csVector3 pos (0, 10, 0);
  // Now we need to position the camera in our world.
  view->GetCamera ()->SetSector (sector);
  view->GetCamera ()->GetTransform ().SetOrigin (pos);

  // Initialize our collider actor.
  collider_actor.SetCollideSystem (cdsys);
  collider_actor.SetEngine (engine);
  collider_actor.SetGravity (9.806);
  // Read the values from config files, and use default values if not set.
  csVector3 legs (GetConfigVector (cfgAcc, "Actor.Legs", "0.2,0.5,0.2"));
  csVector3 body (GetConfigVector (cfgAcc, "Actor.Body", "0.2,1.8,0.2"));
  csVector3 shift (GetConfigVector (cfgAcc, "Actor.Shift", "0.0,-1.7,0.0"));
  collider_actor.InitializeColliders (view->GetCamera (),
        legs, body, shift);

  return true;
}

bool AppAresEdit::CreateWalls ()
{
  if (!LoadLibrary ("/this/data/factories/", "genBox"))
    return ReportError ("Error loading library!");

  vfs->Mount ("/aresnode", "data$/node.zip");
  if (!LoadLibrary ("/aresnode/", "library"))
    return ReportError ("Error loading library!");
  vfs->PopDir ();
  vfs->Unmount ("/aresnode", "data$/node.zip");

  vfs->Mount ("/aresdata", "data$/rack.zip");
  if (!LoadLibrary ("/aresdata/", "library"))
    return ReportError ("Error loading library!");
  vfs->PopDir ();
  vfs->Unmount ("/aresdata", "data$/rack.zip");

  csLoadResult rc = loader->Load ("/lib/krystal/krystal.xml");
  if (!rc.success)
    return ReportError ("Can't load Krystal library file!");

  rc = loader->Load ("/lib/frankie/frankie.xml");
  if (!rc.success)
    return ReportError ("Can't load Frankie library file!");

  if (!LoadLibrary ("/this/data/", "dynworldFactories.xml"))
    return ReportError ("Error loading library!");

  //-------------------------------------
  // Make the floor.
  //-------------------------------------
  vfs->ChDir ("/this/data/landscape");
  if (!loader->LoadMapFile ("world", false))
  {
    ReportError ("Error couldn't load terrain level!");
    return false;
  }
  sector = engine->FindSector ("room");

  // Find the terrain mesh
  csRef<iMeshWrapper> terrainWrapper = engine->FindMeshObject ("Terrain");
  if (!terrainWrapper)
  {
    ReportError("Error cannot find the terrain mesh!");
    return false;
  }

  csRef<iTerrainSystem> terrain =
    scfQueryInterface<iTerrainSystem> (terrainWrapper->GetMeshObject ());
  if (!terrain)
  {
    ReportError("Error cannot find the terrain interface!");
    return false;
  }

  // Create a terrain collider for each cell of the terrain
  for (size_t i = 0; i < terrain->GetCellCount (); i++)
    bullet_dynSys->AttachColliderTerrain (terrain->GetCell (i));

  iLightList* lightList = sector->GetLights ();
  lightList->RemoveAll ();

  //sun = engine->CreateLight("Sun", csVector3(0,40,0),9999999.0f, csColor(1.0f), CS_LIGHT_DYNAMICTYPE_DYNAMIC);
  //sun->SetType(CS_LIGHT_DIRECTIONAL);
  sun = engine->CreateLight("Sun", csVector3(10),9000, csColor(.3,.2,.1));
  lightList->Add (sun);

  camlight = engine->CreateLight(0, csVector3(0,0,0), 10, csColor (.8,.9,1));
  lightList->Add (camlight);

  shaderMgr = csQueryRegistry<iShaderManager> (object_reg);
  strings = csQueryRegistryTagInterface<iShaderVarStringSet> (object_reg, "crystalspace.shader.variablenameset");
  string_sunDirection = strings->Request ("sun direction");

  engine->Prepare ();
  //CS::Lighting::SimpleStaticLighter::ShineLights (sector, engine, 4);

  return true;
}

CurvedFactoryCreator* AppAresEdit::FindFactoryCreator (const char* name)
{
  for (size_t i = 0 ; i < curvedFactoryCreators.GetSize () ; i++)
    if (curvedFactoryCreators[i].name == name)
      return &curvedFactoryCreators[i];
  return 0;
}

static float TestVerticalBeam (const csVector3& start, float distance, iCamera* camera)
{
  csVector3 end = start;
  end.y -= distance;
  iSector* sector = camera->GetSector ();

  csSectorHitBeamResult result = sector->HitBeamPortals (start, end);
  if (result.mesh)
    return result.isect.y;
  else
    return end.y-.1;
}

void AppAresEdit::SpawnItem (const csString& name)
{
  PushUndo ("New");
  csString fname;
  CurvedFactoryCreator* cfc = FindFactoryCreator (name);
  if (cfc)
  {
    curvedFactoryCounter++;
    fname.Format("%s%d", name.GetData (), curvedFactoryCounter);
    iCurvedFactory* curvedFactory = curvedMeshCreator->AddCurvedFactory (fname, name);
    curvedFactory->GenerateFactory ();

    iDynamicFactory* fact = dynworld->AddFactory (fname, cfc->maxradius, cfc->imposterradius);
    fact->AddRigidMesh (csVector3 (0), cfc->mass);
    static_factories.Add (fname);
    curvedFactories.Push (fact);
  }
  else
  {
    fname = name;
  }

  // Use the camera transform.
  iGraphics2D* g2d = g3d->GetDriver2D ();
  iCamera* camera = view->GetCamera ();
  csVector2 v2d (mouseX, g2d->GetHeight () - mouseY);
  csVector3 v3d = camera->InvPerspective (v2d, 50);
  csVector3 startBeam = camera->GetTransform ().GetOrigin ();
  csVector3 endBeam = camera->GetTransform ().This2Other (v3d);

  csSectorHitBeamResult result = sector->HitBeamPortals (startBeam, endBeam);

  float yorigin = factory_to_origin_offset.Get (fname, 1000000.0);

  csVector3 newPosition;
  if (result.mesh)
  {
    newPosition = result.isect;
    if (yorigin < 999999.0)
      newPosition.y -= yorigin;
  }
  else
  {
    newPosition = endBeam - startBeam;
    newPosition.Normalize ();
    newPosition = camera->GetTransform ().GetOrigin () + newPosition * 3.0f;
  }

  csReversibleTransform tc = view->GetCamera ()->GetTransform ();
  csVector3 front = tc.GetFront ();
  front.y = 0;
  tc.LookAt (front, csVector3 (0, 1, 0));
  tc.SetOrigin (newPosition);
  iDynamicObject* dynobj = dynworld->AddObject (fname, tc);

  // Make sure the object is above the ground on all four corners too.
  const csBox3& box = dynobj->GetBBox ();
  float dist = (box.MaxY () - box.MinY ()) * 2.0;
  float y1 = TestVerticalBeam (box.GetCorner (CS_BOX_CORNER_xYz), dist, camera);
  if (yorigin < 999999.0) y1 -= yorigin;
  float y2 = TestVerticalBeam (box.GetCorner (CS_BOX_CORNER_XYz), dist, camera);
  if (yorigin < 999999.0) y2 -= yorigin;
  float y3 = TestVerticalBeam (box.GetCorner (CS_BOX_CORNER_xYZ), dist, camera);
  if (yorigin < 999999.0) y3 -= yorigin;
  float y4 = TestVerticalBeam (box.GetCorner (CS_BOX_CORNER_XYZ), dist, camera);
  if (yorigin < 999999.0) y4 -= yorigin;
  bool changed = false;
  if (y1 > newPosition.y) { newPosition.y = y1; changed = true; }
  if (y2 > newPosition.y) { newPosition.y = y2; changed = true; }
  if (y3 > newPosition.y) { newPosition.y = y3; changed = true; }
  if (y4 > newPosition.y) { newPosition.y = y4; changed = true; }
  if (changed)
  {
    dynobj->MakeKinematic ();
    csReversibleTransform trans = dynobj->GetTransform ();
    printf ("Changed: orig=%g new=%g\n", trans.GetOrigin ().y, newPosition.y); fflush (stdout);
    trans.SetOrigin (newPosition);
    dynobj->SetTransform (trans);
    dynobj->UndoKinematic ();
  }

  if (static_factories.In (fname))
    dynobj->MakeStatic ();
  SetCurrentObject (dynobj);
}

bool AppAresEdit::InitPhysics ()
{
  dyn = csQueryRegistry<iDynamics> (GetObjectRegistry ());
  if (!dyn) return ReportError ("Error loading bullet plugin!");

  dynSys = dyn->CreateSystem ();
  if (!dynSys) return ReportError ("Error creating dynamic system!");
  //dynSys->SetLinearDampener(.3f);
  dynSys->SetRollingDampener(.995f);
  dynSys->SetGravity (csVector3 (0.0f, -9.81f, 0.0f));

  bullet_dynSys = scfQueryInterface<CS::Physics::Bullet::iDynamicSystem> (dynSys);
  bullet_dynSys->SetInternalScale (1.0f);
  bullet_dynSys->SetStepParameters (0.005f, 2, 10);

  return true;
}

bool AppAresEdit::LoadLibrary (const char* path, const char* file)
{
  // Set current VFS dir to the level dir, helps with relative paths in maps
  vfs->PushDir (path);
  if (!loader->LoadLibraryFile (file))
  {
    vfs->PopDir ();
    return ReportError("Couldn't load library file %s!", path);
  }
  vfs->PopDir ();
  return true;
}

