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
            m_watcher.NotifyFilter = NotifyFilters.LastWrite;
            m_watcher.IncludeSubdirectories = true;
            m_watcher.Changed += onChanged;
            m_watcher.EnableRaisingEvents = true;
        }

        private void reloadScript(string path)
        {
            
        }

        private void onChanged(object sender, FileSystemEventArgs args)
        {
            switch (System.IO.Path.GetExtension(args.FullPath.ToLower()))
            {
                case ".cpp":
                    server.reloadScript(args.FullPath.StartsWith(".\\") ? args.FullPath.Substring(2) : args.FullPath);
                    break;
                case ".dae":
                    m_main_form.importModel(args.FullPath);
                    break;
            }
        }
    }
}
