
import {Pipe, ConsoleMessage, ConsoleMessageType} from './io/pipe';
import {clipboard, remote, dialog, shell as electron_shell} from 'electron';

const {Menu, MenuItem} = remote;

import { PromptMessage, TerminalImplementation } from './shell/terminal_implementation';
import { LanguageInterface } from './shell/language_interface';
import { RInterface } from './shell/language_interface_r';
import { JuliaInterface } from './shell/language_interface_julia';

import {Splitter, SplitterOrientation, SplitterEvent} from './ui/splitter';
import {TabPanel, TabJustify, TabEventType} from './ui/tab_panel';
import {DialogManager, DialogSpec, DialogButton} from './ui/dialog';
import {PropertyManager} from './common/properties';
import {MenuUtilities} from './ui/menu_utilities';

import { MultiplexedTerminal } from './shell/multiplexed_terminal';

import {Alert, AlertSpec} from './ui/alert';
import {Editor, EditorEvent, EditorEventType} from './editor/editor';
import {Preferences, PreferencesLoadStatus} from './common/preferences';

import * as Rx from "rxjs";
import * as path from 'path';
import { prototype } from 'stream';

// FIXME: l10n override?
const MenuTemplate = require("../data/menu.json");

// init management pipe for talking to BERT

let management_pipe = new Pipe();
if( process.env['BERT_MANAGEMENT_PIPE']){
  management_pipe.Init({ pipe_name: process.env['BERT_MANAGEMENT_PIPE'] });
  management_pipe.control_messages.subscribe(message => {
    if( message == "shutdown-console" ) Shutdown();
  })
}

// props

let property_manager = new PropertyManager("console-settings", {
  terminal: {}, editor: {}, console: {}
});
let properties = property_manager.properties;

// create splitter (main layout)

let splitter = new Splitter(
  document.getElementById("main-window"), 
  properties.terminal.orientation || SplitterOrientation.Horizontal, 
  properties.terminal.split || 50,
  [
    // we want these to be positive values, but have positive defaults.
    typeof properties.terminal.show_editor === "undefined" || properties.terminal.show_editor, 
    typeof properties.terminal.show_shell === "undefined" || properties.terminal.show_shell 
  ]  
);

// dialogs

let dialog_manager = new DialogManager();

// terminals and tabs

let terminals = new MultiplexedTerminal("#terminal-tabs");

// language connections. 
// FIXME: parameterize, or make these dynamic. in fact, do that (make them dynamic).

let language_interface_types = [
  RInterface, JuliaInterface
];

// active languages, set by pipe connections

let language_interfaces = [];

// hide cursor when busy.
// update: don't do this. it causes stray errors as the tooltip
// is looking for it. might be better to hide it at the CSS layer.

// handle closing
let allow_close = false; // true; // dev // false;
let dev_flags = Number(process.env['BERT_DEV_FLAGS']||0);
if( dev_flags ) allow_close = true;

/** 
 * this is an explicit shutdown command from the main 
 * process. allow closing, then trigger.
 */
const Shutdown = function(){
  console.info("Calling terminals cleanup");
  terminals.CleanUp();  
  
  // editor.Shutdown();

  console.info("Waiting for language shutdown");
  Promise.all(language_interfaces.map(language_interface => 
    language_interface.Shutdown())).then(() => {
    allow_close = true;
    console.info("Calling close");
    remote.getCurrentWindow().close();
  });
}

let Close = function(){
  terminals.CleanUp().then(() => {
    allow_close = true;
    remote.getCurrentWindow().close();
  });
};

window.addEventListener("beforeunload", event => {
 
  if(!allow_close) {
    event.returnValue = false;
    management_pipe.SysCall("hide-console");
  }
  else {
    // Close();
    editor.Shutdown();
  }
});

// construct editor

let editor = new Editor("#editor", properties.editor);
window['editor'] = editor;

editor.events.subscribe(event => {
  if( event.type === EditorEventType.Command ){
    switch(event.message){
    case "execute-selection":
    case "execute-buffer":
      if(event.data.code){
        terminals.Activate(event.data.language);
        terminals.SendCommand("paste", event.data.code);
      }
      break;     
    }
  }
});

// connect/init pipes, languages

let pipe_list = (process.env['BERT_PIPE_NAME']||"").split(";"); // separator?

// wait until we have read prefs once, then set up.
// FIXME: is that necessary? we could just repaint.

Preferences.filter(x => x.preferences).first().subscribe(x => {

  let shell_preferences = x.preferences.shell || {};

  // FIXME: have terminal subscribe to prefs on its own
  terminals.SetPreferences(shell_preferences);

  // FIXME: after languages/tabs are initialized, select tab
  // based on stored preferences (is this going to be a long
  // delay? UX)

  Promise.all(pipe_list.map(pipe_name => {
      return new Promise((resolve, reject) => {
        if(pipe_name){
          let pipe = new Pipe();
          let language = "unknown"; // just for reporting
          pipe.Init({pipe_name}).then(() => {
            pipe.SysCall("get-language").then(response => {
              if( response ){
                console.info( "Pipe", pipe_name, "language response:", response );
                language = response.toString();
                editor.SupportLanguage(language);

                let found = language_interface_types.some(interface_class => {
                  if(interface_class.language_name_ === response ){
                    let instance = new interface_class();
                    instance.label_ = language;
                    language_interfaces.push(instance);
                    instance.InitPipe(pipe, pipe_name);
                    terminals.Add(instance);
                    return true;
                  }
                  return false;
                });
                if(found){
                  pipe.RegisterConsoleMessages();
                }
              }
              console.info( "resolving", language)
              resolve();
            });
          }).catch(e => {
            console.error(e);
            resolve();
          });
        }
        else return resolve();
      });
    })
  ).then(() => {
    console.info( "languages complete");

    terminals.Activate(properties.active_tab||0);

    terminals.active_tab.subscribe(active => {
      if(properties.active_tab !== active) properties.active_tab = active;
    });

  });

});

let alert_instance = new Alert();

// subscribe to preferences to watch for errors
let preferences_status = PreferencesLoadStatus.NotLoaded;
let preferences_error_once = false;
Preferences.subscribe(x => {  
  if(x.status !== preferences_status){

    /*
    if( x.status === PreferencesLoadStatus.Error ){
      editor.status_bar.PushMessage("Preferences error");
    }    
    else if( preferences_status === PreferencesLoadStatus.Error){
      editor.status_bar.PopMessage();
    }
    preferences_status = x.status;
    */

    if(preferences_status === PreferencesLoadStatus.Error && !preferences_error_once){
      preferences_error_once = true;
      alert_instance.Show({
        title: "Preferences Error",
        message: "Please check your preferences file. BERT may not work correctly.",
        timeout: 7
      })
    }
  }
});

// deal with splitter change on drag end 

splitter.events.filter(x => (x === SplitterEvent.EndDrag||x === SplitterEvent.UpdateLayout)).subscribe(x => {
  terminals.UpdateLayout();
  editor.UpdateLayout();
  properties.terminal.split = splitter.split;
  properties.terminal.orientation = splitter.orientation;
});

// construct menus

MenuUtilities.Load(MenuTemplate);
try {
  MenuUtilities.SetLabel( "main.help.version", 
    (MenuUtilities.GetLabel("main.help.version")||"").trim() + " " + process.env.BERT_VERSION );
}
catch(e){
  console.warn( "Error setting version label in menu" );
}

// update to match properties

MenuUtilities.SetCheck("main.view.show-editor", 
  typeof properties.terminal.show_editor === "undefined" || properties.terminal.show_editor);
MenuUtilities.SetCheck("main.view.show-shell", 
  typeof properties.terminal.show_shell === "undefined" || properties.terminal.show_shell);


MenuUtilities.events.subscribe(event => {

  switch(event.id){

  // dev

  case "main.view.reload":
    remote.getCurrentWindow().reload();
    break;
  case "main.view.toggle-developer-tools":
    remote.getCurrentWindow()['toggleDevTools']();
    break;

  // layout

  case "main.view.layout.layout-horizontal":
    splitter.orientation = SplitterOrientation.Horizontal;
    break;
  case "main.view.layout.layout-vertical":
    splitter.orientation = SplitterOrientation.Vertical;
    break;

  case "main.view.show-editor":
    splitter.ShowChild(0, event.item.checked);
    properties.terminal.show_editor = event.item.checked;
    terminals.UpdateLayout();
    editor.UpdateLayout();
    break;

  case "main.view.show-shell":
    splitter.ShowChild(1, event.item.checked);
    properties.terminal.show_shell = event.item.checked;
    terminals.UpdateLayout();
    editor.UpdateLayout();
    break;
    
  // prefs is now handled by editor (just loads). we have 
  // it here to prevent debug logging

  case "main.view.preferences":
    break;

  // ditto

  case "main.help.release-notes":
    break;
    
  // ...

  case "main.help.website":
    electron_shell.openExternal("http://bert-toolkit.com");
    break;

  case "main.help.feedback":
    electron_shell.openExternal("https://bert-toolkit.com/contact");
    break;
    
  default:
    console.info(event.id);
  }

});

let resize_timeout_id = 0;
window.addEventListener("resize", event => {

  if(resize_timeout_id) window.clearTimeout(resize_timeout_id);
  resize_timeout_id = window.setTimeout(() => {
    terminals.UpdateLayout();
    editor.UpdateLayout();
    resize_timeout_id = 0;
  }, 100);

  // console.info("RS", event);

});

TerminalImplementation.events.filter(x => (x.type === "release-focus")).subscribe(x => {
  editor.Focus();
});

window.addEventListener("keydown", event => {

  // we trap Ctrl+Insert and Shift+Insert in this handler,
  // even though they're intended for the shell. for whatever
  // reason the shell doesn't see these keys.

  if(event.ctrlKey){
    switch(event.key){
    case 'e':
      // console.info( "Ctrl+E in editor");
      terminals.Focus();
      break;
    case 'PageUp':
      editor.PreviousTab();
      break;
    case 'PageDown':
      editor.NextTab();
      break;
    case 'Insert':
      terminals.SendCommand("copy");
      break;
    default:
      return;
    }
  }
  else if(event.shiftKey){
    switch(event.key){
    case 'Insert':
      terminals.SendCommand("paste");
      break;
    default:
      return;
    }
  }
  else return;
  
  event.stopPropagation();
  event.preventDefault();

});

/** 
 * was trying to debug something that changed on focus out. this 
 * allows us to pause javascript execution (via breakpoint) in 
 * the future, so we can focus and then prevent future events.
 * /
window["Pause"] = function(in_seconds){
  setTimeout(() => {
    console.info("Pausing!");
  }, in_seconds * 1000);
} 
*/
