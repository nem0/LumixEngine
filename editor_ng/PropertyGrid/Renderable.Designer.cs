namespace editor_ng.PropertyGrid
{
    partial class Renderable
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

        #region Component Designer generated code

        /// <summary> 
        /// Required method for Designer support - do not modify 
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.label1 = new System.Windows.Forms.Label();
            this.label2 = new System.Windows.Forms.Label();
            this.visibleCheckBox = new System.Windows.Forms.CheckBox();
            this.label3 = new System.Windows.Forms.Label();
            this.castShadowsCheckbox = new System.Windows.Forms.CheckBox();
            this.sourceInput = new editor_ng.PropertyGrid.FileInput();
            this.SuspendLayout();
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(1, 1);
            this.label1.Margin = new System.Windows.Forms.Padding(1, 1, 1, 3);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(41, 13);
            this.label1.TabIndex = 1;
            this.label1.Text = "Source";
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(1, 18);
            this.label2.Margin = new System.Windows.Forms.Padding(1, 1, 1, 3);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(37, 13);
            this.label2.TabIndex = 4;
            this.label2.Text = "Visible";
            // 
            // visibleCheckBox
            // 
            this.visibleCheckBox.AutoSize = true;
            this.visibleCheckBox.Location = new System.Drawing.Point(86, 18);
            this.visibleCheckBox.Name = "visibleCheckBox";
            this.visibleCheckBox.Size = new System.Drawing.Size(15, 14);
            this.visibleCheckBox.TabIndex = 5;
            this.visibleCheckBox.UseVisualStyleBackColor = true;
            this.visibleCheckBox.CheckedChanged += new System.EventHandler(this.visibleCheckBox_CheckedChanged);
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Location = new System.Drawing.Point(1, 35);
            this.label3.Margin = new System.Windows.Forms.Padding(1);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(73, 13);
            this.label3.TabIndex = 6;
            this.label3.Text = "Cast shadows";
            // 
            // castShadowsCheckbox
            // 
            this.castShadowsCheckbox.AutoSize = true;
            this.castShadowsCheckbox.Location = new System.Drawing.Point(86, 35);
            this.castShadowsCheckbox.Name = "castShadowsCheckbox";
            this.castShadowsCheckbox.Size = new System.Drawing.Size(15, 14);
            this.castShadowsCheckbox.TabIndex = 7;
            this.castShadowsCheckbox.UseVisualStyleBackColor = true;
            this.castShadowsCheckbox.CheckedChanged += new System.EventHandler(this.castShadowsCheckbox_CheckedChanged);
            // 
            // sourceInput
            // 
            this.sourceInput.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.sourceInput.Filter = null;
            this.sourceInput.Location = new System.Drawing.Point(86, 0);
            this.sourceInput.Margin = new System.Windows.Forms.Padding(0);
            this.sourceInput.Name = "sourceInput";
            this.sourceInput.Size = new System.Drawing.Size(205, 20);
            this.sourceInput.TabIndex = 3;
            this.sourceInput.Value = "";
            // 
            // Renderable
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.Controls.Add(this.castShadowsCheckbox);
            this.Controls.Add(this.label3);
            this.Controls.Add(this.visibleCheckBox);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.sourceInput);
            this.Controls.Add(this.label1);
            this.Name = "Renderable";
            this.Size = new System.Drawing.Size(291, 50);
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Label label1;
        private FileInput sourceInput;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.CheckBox visibleCheckBox;
        private System.Windows.Forms.Label label3;
        private System.Windows.Forms.CheckBox castShadowsCheckbox;
    }
}
