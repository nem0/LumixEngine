using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;

namespace editor_ng
{
    class AssetMonitor
    {
        FileSystemWatcher m_watcher;

        public native.EditorServer server { get; set; }
        public ScriptCompiler script_compiler { get; set; }

        MainForm m_main_form;

        public AssetMonitor(MainForm main_form)
        {
            m_main_form = main_form;
        }

        public void start()
        {
            m_watcher = new FileSystemWatcher();
            m_watcher.Path = ".";
            m_watcher.Filter = "*.*";
            m_watcher.NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName;
            m_watcher.IncludeSubdirectories = true;
            m_watcher.Changed += onChanged;
            m_watcher.Renamed += onChanged;
            m_watcher.EnableRaisingEvents = true;
        }

        private void onChanged(object sender, FileSystemEventArgs args)
        {
            switch (System.IO.Path.GetExtension(args.FullPath.ToLower()))
            {
                case ".cpp":
                    script_compiler.compile(args.FullPath, args.Name, true);
                    break;
                case ".dae":
                    m_main_form.importModel(args.FullPath);
                    break;
            }
        }
    }
}
