﻿using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Runtime.InteropServices;
using System.IO;
using System.Threading;
using System.Windows.Input;
using WeifenLuo.WinFormsUI.Docking;

namespace editor_ng
{
    

    public partial class MainForm : Form
    {
        SceneView m_scene_view;
        PropertyGrid m_property_grid;
        AssetList m_asset_list;
        List<DockContent> m_dock_contents;
        native.EditorServer m_server;
        AssetMonitor m_asset_monitor;
        bool m_finished;
        FileServer.FileServer m_file_server;
        FileServer.FileServerUI m_file_server_ui;
        Notifications m_notifications;
        ScriptCompiler m_script_compiler;
        Log.LogUI m_log_ui;
        string m_universe_filename = "";

        public MainForm()
        {
            InitializeComponent();
            m_scene_view = new SceneView();
            m_server = new native.EditorServer();
            m_log_ui = new Log.LogUI(m_server);
            m_server.create(m_scene_view.panel1.Handle, System.IO.Directory.GetCurrentDirectory());
            m_notifications = new Notifications(this);

            m_file_server = new FileServer.FileServer();
            m_file_server_ui = new FileServer.FileServerUI();
            m_file_server.ui = m_file_server_ui;
            m_asset_monitor = new AssetMonitor();
            m_asset_list = new AssetList(this);
            m_asset_list.main_form = this;
            m_property_grid = new PropertyGrid();
            m_property_grid.main_form = this;
            m_script_compiler = new ScriptCompiler();
            m_script_compiler.onScriptUpdated += ScriptCompiler_onScriptUpdated;
            m_script_compiler.onCompileError += ScriptCompiler_onCompileError;

            m_asset_monitor.script_compiler = m_script_compiler;
            m_file_server.start();
            m_asset_monitor.server = m_server;
            m_scene_view.server = m_server;
            m_asset_list.server = m_server;
            m_property_grid.server = m_server;
            m_asset_monitor.start();
            registerComponentNames();
            m_dock_contents = new List<DockContent>();
            m_dock_contents.Add(m_scene_view);
            m_dock_contents.Add(m_asset_list);
            m_dock_contents.Add(m_property_grid);
            m_dock_contents.Add(m_file_server_ui);
            m_dock_contents.Add(m_log_ui);
            if (System.IO.File.Exists("layout.xml"))
            {
                dockPanel.LoadFromXml("layout.xml", new DeserializeDockContent(GetContentFromPersistString));
            }
        }

        void ScriptCompiler_onScriptUpdated(object sender, EventArgs e)
        {
            ScriptCompiler.ScriptUpdatedEventArgs args = e as ScriptCompiler.ScriptUpdatedEventArgs;
            BeginInvoke((MethodInvoker)(() => {
                m_notifications.showNotification("Script " + args.script_name + " updated");
            }));
        }

        protected IDockContent GetContentFromPersistString(string persistString)
        {
            foreach (DockContent c in m_dock_contents)
            {
                if (c.GetType().ToString() == persistString)
                {
                    return c;
                }
            }
            return null;
        }

        private void registerComponentNames()
        {
            m_component_names = new Dictionary<uint,string>();
            m_component_names[Crc32.Compute(System.Text.Encoding.ASCII.GetBytes("renderable"))] = "renderable";
            m_component_names[Crc32.Compute(System.Text.Encoding.ASCII.GetBytes("physical"))] = "physical";
            m_component_names[Crc32.Compute(System.Text.Encoding.ASCII.GetBytes("physical_controller"))] = "physical controller";
            m_component_names[Crc32.Compute(System.Text.Encoding.ASCII.GetBytes("point_light"))] = "point light";
            m_component_names[Crc32.Compute(System.Text.Encoding.ASCII.GetBytes("player_controller"))] = "player controller";
            m_component_names[Crc32.Compute(System.Text.Encoding.ASCII.GetBytes("camera"))] = "camera";
            m_component_names[Crc32.Compute(System.Text.Encoding.ASCII.GetBytes("script"))] = "script";
            m_component_names[Crc32.Compute(System.Text.Encoding.ASCII.GetBytes("animable"))] = "animable";
        }

        public void tick()
        {
            m_server.update();
            m_scene_view.panel1.Refresh();
            if (m_scene_view.panel1.ContainsFocus)
            {
                bool forward = (Keyboard.GetKeyStates(Key.W) & KeyStates.Down) > 0;
                bool backward = (Keyboard.GetKeyStates(Key.S) & KeyStates.Down) > 0;
                bool left = (Keyboard.GetKeyStates(Key.A) & KeyStates.Down) > 0;
                bool right = (Keyboard.GetKeyStates(Key.D) & KeyStates.Down) > 0;
                bool shift = (Keyboard.GetKeyStates(Key.LeftShift) & KeyStates.Down) > 0;
                if (forward || backward || left || right)
                {
                    m_server.navigate(forward ? m_scene_view.cameraSpeed : (backward ? -m_scene_view.cameraSpeed : 0.0f)
                        , right ? m_scene_view.cameraSpeed : (left ? -m_scene_view.cameraSpeed : 0.0f)
                        , shift ? 1 : 0);
                }
            }
        }

        Dictionary<uint, string> m_component_names;

        public string componentType2Name(uint type)
        {
            string name;
            if(m_component_names.TryGetValue(type, out name))
            {
                return name;
            }
            return "unknown";
        }

        public uint componentName2Type(string name)
        {
            foreach (var a in m_component_names)
            {
                if (a.Value == name)
                {
                    return a.Key;
                }
            }
            return 0;
        }

        private void openToolStripMenuItem_Click(object sender, EventArgs e)
        {
            OpenFileDialog ofd = new OpenFileDialog();
            ofd.Filter = "Level files (.unv)|*.unv|All Files (*.*)|*.*";
            if (ofd.ShowDialog() == DialogResult.OK)
            {
                m_universe_filename = ofd.FileName;
                m_server.openUniverse(ofd.FileName);
            }
        }

        private void saveAsToolStripMenuItem_Click(object sender, EventArgs e)
        {
            SaveFileDialog ofd = new SaveFileDialog();
            ofd.Filter = "Level files (.unv)|*.unv|All Files (*.*)|*.*";
            if (ofd.ShowDialog() == DialogResult.OK)
            {
                m_universe_filename = ofd.FileName;
                m_server.saveUniverseAs(ofd.FileName);
            }
        }

        private void exitToolStripMenuItem_Click(object sender, EventArgs e)
        {
            Application.Exit();
        }

        private void ScriptCompiler_onAllScriptsCompiled(object sender, EventArgs e)
        {
            BeginInvoke((MethodInvoker)(() => {
                m_notifications.showNotification("All scripts compiled");
                m_server.startGameMode(); 
            }));
        }

        private void gameModeToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_script_compiler.onAllScriptsCompiled -= ScriptCompiler_onAllScriptsCompiled; 
            m_script_compiler.onAllScriptsCompiled += ScriptCompiler_onAllScriptsCompiled;
            m_script_compiler.compileAllScripts();
        }

        void ScriptCompiler_onCompileError(object sender, EventArgs e)
        {
            BeginInvoke((MethodInvoker)(() =>
            {
                ScriptCompiler.CompileErrorEventArgs args = e as ScriptCompiler.CompileErrorEventArgs;
                m_log_ui.logMessage(1, "system", "Script " + args.script_name + " failed to compile");
                m_notifications.showNotification("Script " + args.script_name + " failed to compile");
            }));
        }

        private void MainForm_Shown(object sender, EventArgs e)
        {
            m_finished = false;
            while (!m_finished)
            {
                Application.DoEvents();
                tick();
            }
        }

        private void sceneViewToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_scene_view.Show(dockPanel);
        }

        private void propertiesToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_property_grid.Show(dockPanel, DockState.Document);
        }

        private void createEmptyToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_server.createEntity();
        }

        private void assetsToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_asset_list.Show(dockPanel);
        }

        private void MainForm_FormClosing(object sender, FormClosingEventArgs e)
        {
            dockPanel.SaveAsXml("layout.xml");
            m_finished = true;
        }

        private void removeToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_server.removeEntity();
        }

        private void editScriptToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_server.editScript();
        }

        private Dictionary<System.Diagnostics.Process, string> m_import_model_processes = new Dictionary<System.Diagnostics.Process, string>();

        public void importModel()
        {
            OpenFileDialog dlg = new OpenFileDialog();
            if (dlg.ShowDialog() == DialogResult.OK)
            {
                m_notifications.showNotification("Importing model " + dlg.FileName);

                System.Diagnostics.ProcessStartInfo start_info = new System.Diagnostics.ProcessStartInfo("models\\ColladaConv.bat", dlg.FileName);
                start_info.CreateNoWindow = true;
                start_info.WindowStyle = System.Diagnostics.ProcessWindowStyle.Hidden;
                System.Diagnostics.Process process = System.Diagnostics.Process.Start(start_info);
                process.EnableRaisingEvents = true;
                m_import_model_processes.Add(process, dlg.FileName);
                process.Exited += importModelFinished;
            }
        }

        private void importModelToolStripMenuItem_Click(object sender, EventArgs e)
        {
            importModel();
        }

        void importModelFinished(object sender, EventArgs e)
        {
            string filename = m_import_model_processes[sender as System.Diagnostics.Process];
            m_import_model_processes.Remove(sender as System.Diagnostics.Process);
            this.BeginInvoke(new Action(()=>{
                m_notifications.showNotification("Model " + filename + " imported");
            }));
            m_asset_list.refresh();
        }

        private void fileServerToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_file_server_ui.Show(dockPanel);
        }

        private void lookAtSelectedToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_server.lookAtSelected();
        }

        private void saveToolStripMenuItem_Click(object sender, EventArgs e)
        {
            if (m_universe_filename == "")
            {
                saveAsToolStripMenuItem_Click(sender, e);
            }
            else
            {
                m_server.saveUniverseAs(m_universe_filename);
            }
        }

        private void newToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_server.newUniverse();
            m_universe_filename = "";
        }

        private void logToolStripMenuItem_Click(object sender, EventArgs e)
        {
            m_log_ui.Show(dockPanel);
        }
    }
}
