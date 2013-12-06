namespace editor_ng.PropertyGrid
{
    partial class BoxRigidDynamic
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
            this.dynamicCheckbox = new System.Windows.Forms.CheckBox();
            this.label2 = new System.Windows.Forms.Label();
            this.xNumericUpDown = new System.Windows.Forms.NumericUpDown();
            this.label3 = new System.Windows.Forms.Label();
            this.yNumericUpDown = new System.Windows.Forms.NumericUpDown();
            this.zNumericUpDown = new System.Windows.Forms.NumericUpDown();
            this.label4 = new System.Windows.Forms.Label();
            ((System.ComponentModel.ISupportInitialize)(this.xNumericUpDown)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.yNumericUpDown)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.zNumericUpDown)).BeginInit();
            this.SuspendLayout();
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(1, 3);
            this.label1.Margin = new System.Windows.Forms.Padding(1, 1, 1, 3);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(48, 13);
            this.label1.TabIndex = 0;
            this.label1.Text = "Dynamic";
            // 
            // dynamicCheckbox
            // 
            this.dynamicCheckbox.AutoSize = true;
            this.dynamicCheckbox.Location = new System.Drawing.Point(57, 3);
            this.dynamicCheckbox.Name = "dynamicCheckbox";
            this.dynamicCheckbox.Size = new System.Drawing.Size(15, 14);
            this.dynamicCheckbox.TabIndex = 1;
            this.dynamicCheckbox.UseVisualStyleBackColor = true;
            this.dynamicCheckbox.CheckedChanged += new System.EventHandler(this.dynamicCheckbox_CheckedChanged);
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(3, 20);
            this.label2.Margin = new System.Windows.Forms.Padding(1, 1, 1, 3);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(14, 13);
            this.label2.TabIndex = 2;
            this.label2.Text = "X";
            // 
            // xNumericUpDown
            // 
            this.xNumericUpDown.DecimalPlaces = 3;
            this.xNumericUpDown.Font = new System.Drawing.Font("Microsoft Sans Serif", 6.75F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(238)));
            this.xNumericUpDown.Location = new System.Drawing.Point(57, 18);
            this.xNumericUpDown.Maximum = new decimal(new int[] {
            999999,
            0,
            0,
            0});
            this.xNumericUpDown.Name = "xNumericUpDown";
            this.xNumericUpDown.Size = new System.Drawing.Size(120, 18);
            this.xNumericUpDown.TabIndex = 3;
            this.xNumericUpDown.ValueChanged += new System.EventHandler(this.xNumericUpDown_ValueChanged);
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Location = new System.Drawing.Point(3, 37);
            this.label3.Margin = new System.Windows.Forms.Padding(1, 1, 1, 3);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(14, 13);
            this.label3.TabIndex = 4;
            this.label3.Text = "Y";
            // 
            // yNumericUpDown
            // 
            this.yNumericUpDown.DecimalPlaces = 3;
            this.yNumericUpDown.Font = new System.Drawing.Font("Microsoft Sans Serif", 6.75F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(238)));
            this.yNumericUpDown.Location = new System.Drawing.Point(57, 35);
            this.yNumericUpDown.Maximum = new decimal(new int[] {
            999999,
            0,
            0,
            0});
            this.yNumericUpDown.Name = "yNumericUpDown";
            this.yNumericUpDown.Size = new System.Drawing.Size(120, 18);
            this.yNumericUpDown.TabIndex = 5;
            this.yNumericUpDown.ValueChanged += new System.EventHandler(this.yNumericUpDown_ValueChanged);
            // 
            // zNumericUpDown
            // 
            this.zNumericUpDown.DecimalPlaces = 3;
            this.zNumericUpDown.Font = new System.Drawing.Font("Microsoft Sans Serif", 6.75F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(238)));
            this.zNumericUpDown.Location = new System.Drawing.Point(57, 53);
            this.zNumericUpDown.Maximum = new decimal(new int[] {
            999999,
            0,
            0,
            0});
            this.zNumericUpDown.Name = "zNumericUpDown";
            this.zNumericUpDown.Size = new System.Drawing.Size(120, 18);
            this.zNumericUpDown.TabIndex = 6;
            this.zNumericUpDown.ValueChanged += new System.EventHandler(this.zNumericUpDown_ValueChanged);
            // 
            // label4
            // 
            this.label4.AutoSize = true;
            this.label4.Location = new System.Drawing.Point(3, 54);
            this.label4.Margin = new System.Windows.Forms.Padding(1, 1, 1, 3);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(14, 13);
            this.label4.TabIndex = 7;
            this.label4.Text = "Z";
            // 
            // BoxRigidDynamic
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.Controls.Add(this.label4);
            this.Controls.Add(this.zNumericUpDown);
            this.Controls.Add(this.yNumericUpDown);
            this.Controls.Add(this.label3);
            this.Controls.Add(this.xNumericUpDown);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.dynamicCheckbox);
            this.Controls.Add(this.label1);
            this.Name = "BoxRigidDynamic";
            this.Size = new System.Drawing.Size(185, 74);
            ((System.ComponentModel.ISupportInitialize)(this.xNumericUpDown)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.yNumericUpDown)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.zNumericUpDown)).EndInit();
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.CheckBox dynamicCheckbox;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.NumericUpDown xNumericUpDown;
        private System.Windows.Forms.Label label3;
        private System.Windows.Forms.NumericUpDown yNumericUpDown;
        private System.Windows.Forms.NumericUpDown zNumericUpDown;
        private System.Windows.Forms.Label label4;
    }
}
