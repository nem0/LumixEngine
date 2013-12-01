using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;

namespace editor_ng
{
    public partial class PropertyGrid : DockContent
    {
        private int m_selected_entity = -1;
        private bool m_is_position_update = false;

        private native.EditorServer m_server = null;
        public native.EditorServer server 
        {
            get { return m_server; }
            set
            {
                if (m_server != null)
                {
                    m_server.onEntitySelected -= onEntitySelected;
                    m_server.onComponentProperties -= onComponentProperties;
                    m_server.onEntityPosition -= onEntityPosition;
                }
                m_server = value;
                if (m_server != null)
                {
                    m_server.onEntityPosition += onEntityPosition;
                    m_server.onComponentProperties += onComponentProperties;
                    m_server.onEntitySelected += onEntitySelected;
                }
            }
        }

        public MainForm main_form { get; set; }

        private class DataGridViewFilenameCell : DataGridViewTextBoxCell
        {
            public DataGridViewFilenameCell()
                : base()
            {
            }

            public override void InitializeEditingControl(int rowIndex, object 
                initialFormattedValue, DataGridViewCellStyle dataGridViewCellStyle)
            {
                // Set the value of the editing control to the current cell value. 
                base.InitializeEditingControl(rowIndex, initialFormattedValue, 
                    dataGridViewCellStyle);
                FilenameEditingControl ctl = 
                    DataGridView.EditingControl as FilenameEditingControl;
                // Use the default row value when Value property is null. 
                if (this.Value == null)
                {
                    ctl.Value = this.DefaultNewRowValue as string;
                }
                else
                {
                    ctl.Value = this.Value as string;
                }
            }

            public override Type EditType
            {
                get
                {
                    // Return the type of the editing control that CalendarCell uses. 
                    return typeof(FilenameEditingControl);
                }
            }

            public override Type ValueType
            {
                get
                {
                    // Return the type of the value that CalendarCell contains. 

                    return typeof(string);
                }
            }

            public override object DefaultNewRowValue
            {
                get
                {
                    // Use the current date and time as the default value. 
                    return "new";
                }
            }
        }

        class FilenameEditingControl : Panel, IDataGridViewEditingControl
        {
            DataGridView dataGridView;
            private bool valueChanged = false;
            int rowIndex;
            TextBox m_text_box;
            Button m_button;

            public FilenameEditingControl()
            {
                m_text_box = new TextBox();
                m_text_box.Dock = DockStyle.Fill;
                m_text_box.TextChanged += onTextChanged;
                this.Controls.Add(m_text_box);
                m_button = new Button();
                m_button.AutoSize = true;
                m_button.AutoSizeMode = System.Windows.Forms.AutoSizeMode.GrowAndShrink;
                m_button.Text = "...";
                m_button.Dock = DockStyle.Right;
                m_button.Click += onClick;
                this.Controls.Add(m_button);
            }

            public string Value
            {
                get { return m_text_box.Text; }
                set 
                {
                    string dir = System.IO.Directory.GetCurrentDirectory();
                    string str = value as string;
                    str = str.Replace(dir, "");
                    str = str.TrimStart('/', '\\');
                    m_text_box.Text = str; 
                }
            }


            // Implements the IDataGridViewEditingControl.EditingControlFormattedValue  
            // property. 
            public object EditingControlFormattedValue
            {
                get
                {
                    return this.Value;
                }
                set
                {
                    if (value is String)
                    {
                        try
                        {
                            // This will throw an exception of the string is  
                            // null, empty, or not in the format of a date. 
                            this.Value = value as string;
                        }
                        catch
                        {
                            // In the case of an exception, just use the  
                            // default value so we're not left with a null 
                            // value. 
                            this.Value = "new";
                        }
                    }
                }
            }

            // Implements the  
            // IDataGridViewEditingControl.GetEditingControlFormattedValue method. 
            public object GetEditingControlFormattedValue(
                DataGridViewDataErrorContexts context)
            {
                return EditingControlFormattedValue;
            }

            // Implements the  
            // IDataGridViewEditingControl.ApplyCellStyleToEditingControl method. 
            public void ApplyCellStyleToEditingControl(
                DataGridViewCellStyle dataGridViewCellStyle)
            {
                /*this.Font = dataGridViewCellStyle.Font;
                this.CalendarForeColor = dataGridViewCellStyle.ForeColor;
                this.CalendarMonthBackground = dataGridViewCellStyle.BackColor;*/
            }

            // Implements the IDataGridViewEditingControl.EditingControlRowIndex  
            // property. 
            public int EditingControlRowIndex
            {
                get
                {
                    return rowIndex;
                }
                set
                {
                    rowIndex = value;
                }
            }

            // Implements the IDataGridViewEditingControl.EditingControlWantsInputKey  
            // method. 
            public bool EditingControlWantsInputKey(
                Keys key, bool dataGridViewWantsInputKey)
            {
                // Let the DateTimePicker handle the keys listed. 
                switch (key & Keys.KeyCode)
                {
                    case Keys.Left:
                    case Keys.Up:
                    case Keys.Down:
                    case Keys.Right:
                    case Keys.Home:
                    case Keys.End:
                    case Keys.PageDown:
                    case Keys.PageUp:
                        return true;
                    default:
                        return !dataGridViewWantsInputKey;
                }
            }

            // Implements the IDataGridViewEditingControl.PrepareEditingControlForEdit  
            // method. 
            public void PrepareEditingControlForEdit(bool selectAll)
            {
                // No preparation needs to be done.
            }

            // Implements the IDataGridViewEditingControl 
            // .RepositionEditingControlOnValueChange property. 
            public bool RepositionEditingControlOnValueChange
            {
                get
                {
                    return false;
                }
            }

            // Implements the IDataGridViewEditingControl 
            // .EditingControlDataGridView property. 
            public DataGridView EditingControlDataGridView
            {
                get
                {
                    return dataGridView;
                }
                set
                {
                    dataGridView = value;
                }
            }

            // Implements the IDataGridViewEditingControl 
            // .EditingControlValueChanged property. 
            public bool EditingControlValueChanged
            {
                get
                {
                    return valueChanged;
                }
                set
                {
                    valueChanged = value;
                }
            }

            // Implements the IDataGridViewEditingControl 
            // .EditingPanelCursor property. 
            public Cursor EditingPanelCursor
            {
                get
                {
                    return base.Cursor;
                }
            }

            private void onTextChanged(object sender, EventArgs args)
            {
                // Notify the DataGridView that the contents of the cell 
                // have changed.
                valueChanged = true;
                this.EditingControlDataGridView.NotifyCurrentCellDirty(true);
                //base.OnValueChanged(eventargs);
            }

            private void onClick(object sender, EventArgs args)
            {
                OpenFileDialog ofd = new OpenFileDialog();
                if (ofd.ShowDialog() == DialogResult.OK)
                {
                    string value = ofd.FileName;
                    string dir = System.IO.Directory.GetCurrentDirectory();
                    value = (value as string).Replace(dir, "");
                    value = (value as string).TrimStart('/', '\\');
                    m_text_box.Text = value;
                    this.EditingControlDataGridView.EndEdit();
                }
            }
        }


        private void onEntityPosition(object sender, EventArgs args)
        {
            BeginInvoke(new Action(() =>
            {
                native.EditorServer.EntityPositionArgs e = args as native.EditorServer.EntityPositionArgs;
                if (m_selected_entity == e.uid)
                {
                    m_is_position_update = true;
                    xPositionTextBox.Text = e.x.ToString();
                    yPositionTextBox.Text = e.y.ToString();
                    zPositionTextBox.Text = e.z.ToString();
                    m_is_position_update = false;
                }
            }));
        }

        private void onComponentProperties(object sender, EventArgs args)
        {
            BeginInvoke(new Action(() =>
            {
                native.EditorServer.ComponentPropertiesArgs e = args as native.EditorServer.ComponentPropertiesArgs;
                foreach (Control c in panel1.Controls)
                {
                    if (c is DataGridView && c.Tag != null && (uint)c.Tag == e.cmp_type)
                    {
                        DataGridView dgv = c as DataGridView;
                        dgv.Rows.Clear();
                        for (int i = 0; i < e.names.Length; ++i)
                        {
                            switch (e.types[i])
                            {
                                case 2: // bool
                                    {
                                        int n = dgv.Rows.Add(new object[] { e.names[i], e.values[i] == "true" });
                                        DataGridViewCheckBoxCell cell = new DataGridViewCheckBoxCell();
                                        dgv.Rows[n].Cells[1] = cell;
                                        cell.Value = e.values[i] == "true";
                                    }
                                    break;
                                case 1: // decimal
                                    dgv.Rows.Add(new object[] { e.names[i], e.values[i] });
                                    break;
                                case 0: // filename
                                    {
                                        DataGridViewFilenameCell cell = new DataGridViewFilenameCell();
                                        int n = dgv.Rows.Add(new object[] { e.names[i], e.values[i] });
                                        dgv.Rows[n].Cells[1] = cell;
                                        cell.Value = e.values[i];
                                    }
                                    break;
                                default:
                                    dgv.Rows.Add(new object[] { e.names[i], e.values[i] });
                                    break;
                            }
                        }
                        int h = 0;
                        foreach (DataGridViewRow row in dgv.Rows)
                        {
                            h += row.Height;
                        }
                        Size s = new Size();
                        s.Height = h + 5;
                        s.Width = dgv.ClientSize.Width;
                        dgv.ClientSize = s;
                        break;
                    }
                }
            }));
        }

        private void onCellEndEdit(object sender, EventArgs args)
        {
            DataGridView dgv = sender as DataGridView;
            DataGridViewCellEventArgs e = args as DataGridViewCellEventArgs;
            server.setComponentProperty((uint)dgv.Tag, dgv.Rows[e.RowIndex].Cells[0].Value as string, dgv.Rows[e.RowIndex].Cells[1].Value.ToString());
        }

        private void onCreateComponentClicked(object sender, EventArgs args)
        {
            Form f = new Form();
            f.ControlBox = false;
            f.MaximizeBox = false;
            f.MinimizeBox = false;
            f.FormBorderStyle = FormBorderStyle.None;
            ListBox lb = new ListBox();
            lb.Dock = DockStyle.Fill;
            lb.Items.Add("physical");
            lb.Items.Add("physical_controller");
            lb.Items.Add("point_light");
            lb.Items.Add("renderable");
            lb.Items.Add("script");
            lb.Items.Add("animable");
            lb.DoubleClick += onNewComponentSelected;
            f.Size = new Size((sender as Control).Size.Width, lb.Size.Height);
            f.Controls.Add(lb);
            f.Location = (sender as Control).PointToScreen(new Point(0, 0));
            f.StartPosition = FormStartPosition.Manual;
            f.ShowDialog(this);
        }

        private void onNewComponentSelected(object sender, EventArgs args)
        {
            ListBox lb = sender as ListBox;
            if (lb.SelectedItem != null)
            {
                server.createComponent(Crc32.Compute(lb.SelectedItem as string));
            }
            (lb.Parent as Form).Close();
        }

        private uint m_selected_component = 0;

        private void onCreateComponentDragEnter(object sender, DragEventArgs args)
        {
            args.Effect = DragDropEffects.Link;
        }

     

        private void onCreateComponentDragDrop(object sender, DragEventArgs args)
        {
            if(args.Data.GetDataPresent(typeof(string)))
            {
                string dir = System.IO.Directory.GetCurrentDirectory(); 
                string file = (string)args.Data.GetData(typeof(string));
                file = file.Replace(dir, "");
                file = file.TrimStart('/', '\\');
                if (file.IndexOf("scene.xml") >= 0)
                {
                    server.createComponent(Crc32.Compute("renderable"));
                    server.setComponentProperty(Crc32.Compute("renderable"), "path", file);
                    server.requestComponentProperties(Crc32.Compute("renderable"));
                }
                else if (file.IndexOf(".phy") >= 0)
                {
                    server.createComponent(Crc32.Compute("physical"));
                    server.setComponentProperty(Crc32.Compute("physical"), "source", file);
                    server.requestComponentProperties(Crc32.Compute("physical"));
                }
            }
        }
        
        private void onEntitySelected(object sender, EventArgs args)
        {
            BeginInvoke(new Action(() =>
            {
                panel1.Controls.Clear();
                Button create_cmp_button = new Button();
                create_cmp_button.AllowDrop = true;
                create_cmp_button.DragEnter += onCreateComponentDragEnter;
                create_cmp_button.DragDrop += onCreateComponentDragDrop;
                create_cmp_button.Dock = DockStyle.Top;
                create_cmp_button.Text = "Create component";
                panel1.Controls.Add(create_cmp_button);
                create_cmp_button.Click += onCreateComponentClicked;
                native.EditorServer.EntitySelectedArgs e = args as native.EditorServer.EntitySelectedArgs;
                m_selected_entity = e.uid;
                entityIDtextBox.Text = e.uid.ToString();
                if (e.cmps != null)
                {
                    foreach (uint cmp in e.cmps)
                    {
                        DataGridView dgv = new DataGridView();
                        dgv.CurrentCellDirtyStateChanged += dgv_CurrentCellDirtyStateChanged;
                        dgv.Tag = cmp;
                        dgv.Columns.Add("name", "name");
                        dgv.Columns[0].ReadOnly = true;
                        dgv.Columns.Add("value", "value");
                        dgv.Columns[0].FillWeight = 100;
                        dgv.Columns[0].AutoSizeMode = DataGridViewAutoSizeColumnMode.AllCells;
                        dgv.Columns[0].MinimumWidth = 100;
                        dgv.Columns[1].FillWeight = 100;
                        dgv.Columns[1].AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill;
                        dgv.Dock = DockStyle.Top;
                        dgv.AllowUserToAddRows = false;
                        dgv.AllowUserToDeleteRows = false;
                        dgv.RowHeadersVisible = false;
                        dgv.ColumnHeadersVisible = false;
                        dgv.CellEndEdit += onCellEndEdit;
                        panel1.Controls.Add(dgv);
                        Label l = new Label();
                        l.Text = main_form.componentType2Name(cmp);
                        l.Dock = DockStyle.Top;
                        l.Tag = dgv;
                        dgv.Margin = new Padding(0);
                        l.MouseClick += (a, b) =>
                        {
                            if ((b as MouseEventArgs).Button == MouseButtons.Left)
                            {
                                DataGridView tmp = ((a as Label).Tag as DataGridView);
                                if (tmp.Visible)
                                    tmp.Hide();
                                else
                                    tmp.Show();
                            }
                            else if ((b as MouseEventArgs).Button == MouseButtons.Right)
                            {
                                m_selected_component = (uint)((a as Control).Tag as Control).Tag;
                                componentContextMenu.Show(a as Control, 0, (a as Control).Size.Height);
                            }
                        };
                        panel1.Controls.Add(l);

                        server.requestComponentProperties(cmp);
                    }
                    server.requestPosition();
                }
            }));
        }

        void dgv_CurrentCellDirtyStateChanged(object sender, EventArgs e)
        {
            (sender as DataGridView).EndEdit();
        }

        public PropertyGrid()
        {
            InitializeComponent();
        }

        private void removeComponentToolStripMenuItem_Click(object sender, EventArgs e)
        {
            server.removeComponent(m_selected_component);
        }

        [System.Diagnostics.DebuggerStepThrough]
        private void xPositionTextBox_TextChanged(object sender, EventArgs e)
        {
            if (!m_is_position_update)
            {
                server.setPosition(m_selected_entity, Convert.ToSingle(xPositionTextBox.Text), Convert.ToSingle(yPositionTextBox.Text), Convert.ToSingle(zPositionTextBox.Text));
            }
        }
    }
}
