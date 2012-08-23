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

#include "../apparesed.h"
#include "../aresview.h"
#include "entityparameters.h"
#include "uimanager.h"
#include "edcommon/listctrltools.h"
#include "edcommon/uitools.h"

#include "celtool/stdparams.h"
#include "physicallayer/entitytpl.h"

//--------------------------------------------------------------------------

BEGIN_EVENT_TABLE(EntityParameterDialog, wxDialog)
  EVT_BUTTON (XRCID("ok_Button"), EntityParameterDialog :: OnOkButton)
  EVT_BUTTON (XRCID("cancel_Button"), EntityParameterDialog :: OnCancelButton)
END_EVENT_TABLE()

//--------------------------------------------------------------------------

struct Par
{
  csString name;
  celDataType type;
  csString value;
  Value* child;
};

class ParameterListValue : public StandardCollectionValue
{
private:
  Value* NewChild (const char* name, const char* type, const char* value)
  {
    return NewCompositeChild (
	VALUE_STRING, "name", name,
	VALUE_STRING, "type", type,
	VALUE_STRING, "value", value,
	VALUE_NONE);
  }

  csArray<Par> parameters;

protected:
  virtual void UpdateChildren ()
  {
    if (!dirty) return;
    dirty = false;
    ReleaseChildren ();
    for (size_t i = 0 ; i < parameters.GetSize () ; i++)
    {
      Par& par = parameters[i];
      par.child = NewChild (par.name, celParameterTools::GetTypeName (par.type), par.value);
    }
  }

public:
  ParameterListValue () { }
  virtual ~ParameterListValue () { }

  csArray<Par>& GetParameters () { return parameters; }

  virtual bool DeleteValue (Value* child)
  {
    for (size_t i = 0 ; i < parameters.GetSize () ; i++)
      if (parameters[i].child == child)
      {
	parameters.DeleteIndex (i);
	dirty = true;
	FireValueChanged ();
	return true;
      }
    return false;
  }
  virtual Value* NewValue (size_t idx, Value* selectedValue, const DialogResult& suggestion)
  {
    csString typeName = suggestion.Get ("type", (const char*)0);
    Par p;
    p.name = suggestion.Get ("name", (const char*)0);
    p.type = celParameterTools::GetType (typeName);
    p.value = suggestion.Get ("value", (const char*)0);
    p.child = NewChild (p.name, typeName, p.value);
    parameters.Push (p);
    dirty = true;	// Force refresh because we did an update.
    FireValueChanged ();
    return p.child;
  }

};


//--------------------------------------------------------------------------

void EntityParameterDialog::OnOkButton (wxCommandEvent& event)
{
  csRef<iCelPlLayer> pl = csQueryRegistry<iCelPlLayer> (uiManager->GetApp ()->GetObjectRegistry ());

  csString entityName = object->GetEntityName ();
  iCelEntityTemplate* tpl = object->GetEntityTemplate ();
  csString tplName;
  if (tpl) tplName = tpl->GetName ();
  csString overrideTplName = UITools::GetValue (this, "template_Text");
  if (!overrideTplName.IsEmpty ())
    tplName = overrideTplName;

  csRef<celVariableParameterBlock> params;
  csArray<Par>& pars = parameters->GetParameters ();
  if (pars.GetSize () > 0)
  {
    params.AttachNew (new celVariableParameterBlock ());
    for (size_t i = 0 ; i < pars.GetSize () ; i++)
    {
      Par& p = pars[i];
      celData in, out;
      in.Set (p.value);
      celParameterTools::Convert (in, p.type, out);

      params->AddParameter (pl->FetchStringID (p.name), out);
    }
  }

  object->SetEntity (
      entityName.IsEmpty () ? 0 : entityName.GetData (),
      tplName.IsEmpty () ? 0 : tplName.GetData (),
      params);

  EndModal (TRUE);
}

void EntityParameterDialog::OnCancelButton (wxCommandEvent& event)
{
  EndModal (TRUE);
}

void EntityParameterDialog::Show (iDynamicObject* object)
{
  EntityParameterDialog::object = object;

  csArray<Par>& pars = parameters->GetParameters ();
  pars.DeleteAll ();

  iCelParameterBlock* params = object->GetEntityParameters ();
  if (params)
  {
    csRef<iCelPlLayer> pl = csQueryRegistry<iCelPlLayer> (uiManager->GetApp ()->GetObjectRegistry ());
    for (size_t i = 0 ; i < params->GetParameterCount () ; i++)
    {
      celDataType t;
      csStringID id = params->GetParameterDef (i, t);
      const celData* data = params->GetParameterByIndex (i);

      Par p;
      p.name = pl->FetchString (id);
      p.type = t;
      celParameterTools::ToString (*data, p.value);
      pars.Push (p);
    }
  }

  parameters->Refresh ();

  iCelEntityTemplate* tpl = object->GetEntityTemplate ();
  csString tplName;
  if (tpl) tplName = tpl->GetName ();
  csString factoryName = object->GetFactory ()->GetName ();
  if (factoryName != tplName)
    UITools::SetValue (this, "template_Text", tplName);

  ShowModal ();
}

EntityParameterDialog::EntityParameterDialog (wxWindow* parent, UIManager* uiManager) :
  View (this),
  uiManager (uiManager)
{
  wxXmlResource::Get()->LoadDialog (this, parent, wxT ("EntityParameterDialog"));

  csRef<iUIDialog> dialog = uiManager->CreateDialog (this, "Create Parameter");
  dialog->AddRow ();
  dialog->AddLabel ("Name:");
  dialog->AddText ("name");
  dialog->AddRow ();
  dialog->AddLabel ("Value:");
  dialog->AddText ("value");
  dialog->AddRow ();
  dialog->AddLabel ("Type:");
  dialog->AddChoice ("type", "string", "float", "long", "bool", "vector2",
      "vector3", "color", (const char*)0);

  DefineHeading ("parameter_List", "Name,Type,Value", "name,type,value");
  parameters.AttachNew (new ParameterListValue ());
  Bind (parameters, "parameter_List");
  wxListCtrl* parameter_List = XRCCTRL (*this, "parameter_List", wxListCtrl);
  AddAction (parameter_List, NEWREF (Action, new NewChildDialogAction (parameters, dialog)));
  AddAction (parameter_List, NEWREF (Action, new DeleteChildAction (parameters)));
}

EntityParameterDialog::~EntityParameterDialog ()
{
}


