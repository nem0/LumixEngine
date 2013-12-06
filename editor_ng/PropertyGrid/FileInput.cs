using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Data;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace editor_ng.PropertyGrid
{
    public partial class FileInput : UserControl
    {
        public event EventHandler Changed;
        public class ChangedEventArgs : EventArgs
        {
            public string filename;
        }

        public string Value
        {
            get { return valueTextBox.Text; }
            set { valueTextBox.Text = value; }
        }

        public string Filter { get; set; }

        private void fireChangedEvent()
        {
            if (Changed != null)
            {
                ChangedEventArgs args = new ChangedEventArgs();
                args.filename = valueTextBox.Text;
                Changed(this, args);
            }
        }

        public FileInput()
        {
            InitializeComponent();
            valueTextBox.TextChanged += valueTextBox_TextChanged;
        }

        void valueTextBox_TextChanged(object sender, EventArgs e)
        {
            fireChangedEvent();
        }

        private void browseButton_Click(object sender, EventArgs e)
        {
            OpenFileDialog ofd = new OpenFileDialog();
            ofd.Filter = Filter;
            if(ofd.ShowDialog() == DialogResult.OK)
            {
                string dir = System.IO.Directory.GetCurrentDirectory();
                string str = ofd.FileName;
                str = str.Replace(dir, "");
                str = str.TrimStart('/', '\\');
                valueTextBox.Text = str;
                fireChangedEvent();
            }
        }
    }
}
