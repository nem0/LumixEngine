using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;

namespace editor_ng.FileServer
{
    public partial class FileServerUI : DockContent
    {
        public FileServerUI()
        {
            InitializeComponent();
        }

        public void onFileNotFound(int idx, string path)
        {
            if (IsHandleCreated)
            {
                BeginInvoke(new Action(() => {
                    fileList.Rows.Add(new [] {idx.ToString(), path, "NOT FOUND"});
                }));
            }
        }

        public void onSendFile(int idx, string path, int size)
        {
            if (IsHandleCreated)
            {
                BeginInvoke(new Action(() => {
                    fileList.Rows.Add(new[] { idx.ToString(), path, size.ToString() });
                }));
            }
        }
    }
}
