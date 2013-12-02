namespace editor_ng
{
    partial class PropertyGrid
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.components = new System.ComponentModel.Container();
            this.entityIDtextBox = new System.Windows.Forms.TextBox();
            this.label1 = new System.Windows.Forms.Label();
            this.panel1 = new System.Windows.Forms.Panel();
            this.componentContextMenu = new System.Windows.Forms.ContextMenuStrip(this.components);
            this.removeComponentToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.xPositionTextBox = new System.Windows.Forms.TextBox();
            this.label2 = new System.Windows.Forms.Label();
            this.label3 = new System.Windows.Forms.Label();
            this.yPositionTextBox = new System.Windows.Forms.TextBox();
            this.label4 = new System.Windows.Forms.Label();
            this.zPositionTextBox = new System.Windows.Forms.TextBox();
            this.label5 = new System.Windows.Forms.Label();
            this.entityNameTextBox = new System.Windows.Forms.TextBox();
            this.componentContextMenu.SuspendLayout();
            this.SuspendLayout();
            // 
            // entityIDtextBox
            // 
            this.entityIDtextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.entityIDtextBox.Enabled = false;
            this.entityIDtextBox.Location = new System.Drawing.Point(71, 6);
            this.entityIDtextBox.Name = "entityIDtextBox";
            this.entityIDtextBox.ReadOnly = true;
            this.entityIDtextBox.Size = new System.Drawing.Size(147, 20);
            this.entityIDtextBox.TabIndex = 3;
            this.entityIDtextBox.Text = "10";
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(12, 9);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(50, 13);
            this.label1.TabIndex = 2;
            this.label1.Text = "Entity ID:";
            // 
            // panel1
            // 
            this.panel1.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.panel1.AutoScroll = true;
            this.panel1.Location = new System.Drawing.Point(0, 94);
            this.panel1.Name = "panel1";
            this.panel1.Size = new System.Drawing.Size(224, 549);
            this.panel1.TabIndex = 4;
            // 
            // componentContextMenu
            // 
            this.componentContextMenu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.removeComponentToolStripMenuItem});
            this.componentContextMenu.Name = "componentContextMenu";
            this.componentContextMenu.Size = new System.Drawing.Size(185, 26);
            // 
            // removeComponentToolStripMenuItem
            // 
            this.removeComponentToolStripMenuItem.Name = "removeComponentToolStripMenuItem";
            this.removeComponentToolStripMenuItem.Size = new System.Drawing.Size(184, 22);
            this.removeComponentToolStripMenuItem.Text = "Remove Component";
            this.removeComponentToolStripMenuItem.Click += new System.EventHandler(this.removeComponentToolStripMenuItem_Click);
            // 
            // xPositionTextBox
            // 
            this.xPositionTextBox.Location = new System.Drawing.Point(31, 57);
            this.xPositionTextBox.Name = "xPositionTextBox";
            this.xPositionTextBox.Size = new System.Drawing.Size(46, 20);
            this.xPositionTextBox.TabIndex = 0;
            this.xPositionTextBox.Text = "0";
            this.xPositionTextBox.TextChanged += new System.EventHandler(this.xPositionTextBox_TextChanged);
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(12, 61);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(14, 13);
            this.label2.TabIndex = 6;
            this.label2.Text = "X";
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Location = new System.Drawing.Point(82, 61);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(14, 13);
            this.label3.TabIndex = 8;
            this.label3.Text = "Y";
            // 
            // yPositionTextBox
            // 
            this.yPositionTextBox.Location = new System.Drawing.Point(101, 57);
            this.yPositionTextBox.Name = "yPositionTextBox";
            this.yPositionTextBox.Size = new System.Drawing.Size(46, 20);
            this.yPositionTextBox.TabIndex = 7;
            this.yPositionTextBox.Text = "0";
            this.yPositionTextBox.TextChanged += new System.EventHandler(this.xPositionTextBox_TextChanged);
            // 
            // label4
            // 
            this.label4.AutoSize = true;
            this.label4.Location = new System.Drawing.Point(152, 61);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(14, 13);
            this.label4.TabIndex = 10;
            this.label4.Text = "Z";
            // 
            // zPositionTextBox
            // 
            this.zPositionTextBox.Location = new System.Drawing.Point(171, 57);
            this.zPositionTextBox.Name = "zPositionTextBox";
            this.zPositionTextBox.Size = new System.Drawing.Size(46, 20);
            this.zPositionTextBox.TabIndex = 9;
            this.zPositionTextBox.Text = "0";
            this.zPositionTextBox.TextChanged += new System.EventHandler(this.xPositionTextBox_TextChanged);
            // 
            // label5
            // 
            this.label5.AutoSize = true;
            this.label5.Location = new System.Drawing.Point(12, 35);
            this.label5.Name = "label5";
            this.label5.Size = new System.Drawing.Size(38, 13);
            this.label5.TabIndex = 11;
            this.label5.Text = "Name:";
            // 
            // entityNameTextBox
            // 
            this.entityNameTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.entityNameTextBox.Location = new System.Drawing.Point(71, 32);
            this.entityNameTextBox.Name = "entityNameTextBox";
            this.entityNameTextBox.Size = new System.Drawing.Size(147, 20);
            this.entityNameTextBox.TabIndex = 12;
            this.entityNameTextBox.Text = "name";
            this.entityNameTextBox.TextChanged += new System.EventHandler(this.entityNameTextBox_TextChanged);
            // 
            // PropertyGrid
            // 
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.None;
            this.ClientSize = new System.Drawing.Size(230, 643);
            this.Controls.Add(this.label5);
            this.Controls.Add(this.entityNameTextBox);
            this.Controls.Add(this.label4);
            this.Controls.Add(this.zPositionTextBox);
            this.Controls.Add(this.label3);
            this.Controls.Add(this.yPositionTextBox);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.panel1);
            this.Controls.Add(this.xPositionTextBox);
            this.Controls.Add(this.label1);
            this.Controls.Add(this.entityIDtextBox);
            this.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(238)));
            this.HideOnClose = true;
            this.Name = "PropertyGrid";
            this.Text = "Properties";
            this.componentContextMenu.ResumeLayout(false);
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.TextBox entityIDtextBox;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.Panel panel1;
        private System.Windows.Forms.ContextMenuStrip componentContextMenu;
        private System.Windows.Forms.ToolStripMenuItem removeComponentToolStripMenuItem;
        private System.Windows.Forms.TextBox xPositionTextBox;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.Label label3;
        private System.Windows.Forms.TextBox yPositionTextBox;
        private System.Windows.Forms.Label label4;
        private System.Windows.Forms.TextBox zPositionTextBox;
        private System.Windows.Forms.Label label5;
        private System.Windows.Forms.TextBox entityNameTextBox;

    }
}