#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <gumbo.h>

#define LAST_ID_FILE "/tmp/301mail_last_id.txt"
#define LAST_ERR_FILE "/tmp/301mail_last_err.txt"

#ifndef ACCOUNT_ID
#error "ACCOUNT_ID is not defined! Please define it during compilation."
#endif
#ifndef AUTH_TOKEN
#error "AUTH_TOKEN is not defined! Please define it during compilation."
#endif
#ifndef TG_URL
#error "AUTH_TOKEN is not defined! Please define it during compilation."
#endif
#ifndef TG_CHAT_ID
#error "AUTH_TOKEN is not defined! Please define it during compilation."
#endif

struct MemoryStruct {
	char *memory;
	size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;
	char *ptr = realloc(mem->memory, mem->size + realsize + 1);
	if (!ptr) return 0;
	mem->memory = ptr;
	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;
	return realsize;
}

// 简单的 HTML 标签剥离（弃用了）
void _strip_html(char *str) {
	int i = 0, j = 0, in_tag = 0;
	while (str[i]) {
		if (str[i] == '<') in_tag = 1;
		else if (str[i] == '>') in_tag = 0;
		else if (!in_tag) str[j++] = str[i];
		i++;
	}
	str[j] = '\0';
}

// ================== Gemini写的 ==================
void _gumbo_walker(GumboNode* node, char* dest, size_t* cur_len, size_t max_buf) {
	if (node->type == GUMBO_NODE_TEXT) {
		size_t text_len = strlen(node->v.text.text);
		if (*cur_len + text_len < max_buf - 1) {
			strcat(dest, node->v.text.text);
			*cur_len += text_len;
		}
		return;
	}
	if (node->type == GUMBO_NODE_ELEMENT &&
		node->v.element.tag != GUMBO_TAG_SCRIPT &&
		node->v.element.tag != GUMBO_TAG_STYLE) {
		
		GumboVector* children = &node->v.element.children;
		for (unsigned int i = 0; i < children->length; ++i) {
			_gumbo_walker((GumboNode*)children->data[i], dest, cur_len, max_buf);
			
			// 碰到这些标签自动补个空格或换行，排版更自然喵
			if (node->v.element.tag == GUMBO_TAG_BR || node->v.element.tag == GUMBO_TAG_P || node->v.element.tag == GUMBO_TAG_DIV) {
				if (*cur_len < max_buf - 2 && dest[*cur_len-1] != '\n') {
					strcat(dest, "\n");
					(*cur_len)++;
				}
			}
		}
	}
}
void strip_html(char *str) {
	if (!str || strlen(str) == 0) return;

	// 复制一份原内容，因为我们需要解析它
	char *raw_html = strdup(str);
	GumboOutput* output = gumbo_parse(raw_html);
	
	// 清空原字符串，准备重新填充
	str[0] = '\0';
	size_t cur_len = 0;
	
	// 限制最大长度，防止溢出原缓冲区（假设原缓冲区足够大）
	// 这里我们先取一个保守值，或者由主人在外部 crop
	_gumbo_walker(output->root, str, &cur_len, 8192); 

	gumbo_destroy_output(&kGumboDefaultOptions, output);
	free(raw_html);
}
// ==============================================

// 裁剪函数
void crop_copy(char *dest, const char *src, size_t max_len) {
	size_t src_len = strlen(src);
	if (src_len <= max_len) {
		strcpy(dest, src);
	} else {
		strncpy(dest, src, max_len);
		dest[max_len] = '\0';
		strcat(dest, "...");
	}
}

// 发送 TG 消息
void send_tg_msg(const char *text) {
	CURL *curl = curl_easy_init();
	if (curl) {
		cJSON *root = cJSON_CreateObject();
		cJSON_AddStringToObject(root, "chat_id", TG_CHAT_ID);
		cJSON_AddStringToObject(root, "text", text);
		cJSON_AddStringToObject(root, "parse_mode", "Markdown");
		char *post_data = cJSON_PrintUnformatted(root);
		struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");

		curl_easy_setopt(curl, CURLOPT_URL, TG_URL);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK) fprintf(stderr, "Failed to send TG message: %s\n", curl_easy_strerror(res));

		cJSON_Delete(root);
		free(post_data);
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
	}
}

// 检查是否为重复的错误
int is_duplicate_error(const char *current_err) {
	char last_err[4096] = {0};
	FILE *fp = fopen(LAST_ERR_FILE, "r");
	if (fp) {
		fgets(last_err, sizeof(last_err), fp);
		fclose(fp);
	}
	return strcmp(last_err, current_err) == 0;
}

int main() {
	long last_id = 0;
	FILE *fp = fopen(LAST_ID_FILE, "r");
	if (fp) { fscanf(fp, "%ld", &last_id); fclose(fp); }

	CURL *curl = curl_easy_init();
	if (!curl) return 1;

	struct MemoryStruct chunk = {malloc(1), 0};
	char url[512];
	snprintf(url, sizeof(url), "https://301333.xyz/api/email/latest?emailId=%ld&accountId=%d&allReceive=0", last_id, ACCOUNT_ID);

	struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
	char auth_hdr[1024];
	snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: %s", AUTH_TOKEN);
	headers = curl_slist_append(headers, auth_hdr);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

	CURLcode res = curl_easy_perform(curl);
	if (res == CURLE_OK) {
		cJSON *json = cJSON_Parse(chunk.memory);
		if (!json) {
			fprintf(stderr, "JSON Parse Error: Invalid response from server.\n");
			free(chunk.memory);
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);
			return 1;
		}

		cJSON *code = cJSON_GetObjectItem(json, "code");
		if (code && code->valueint != 200) {
			// 当 code 不为 200
			if (!is_duplicate_error(chunk.memory)) {
				char err_msg[4096];
				snprintf(err_msg, sizeof(err_msg), "⚠️ *API Error Alert!*\n\n```json\n%s\n```", chunk.memory);
				send_tg_msg(err_msg);

				// 记录错误
				fp = fopen(LAST_ERR_FILE, "w");
				if (fp) { fputs(chunk.memory, fp); fclose(fp); }
			}
			fprintf(stderr, "API Rejected: Code %d\n", code->valueint);
			cJSON_Delete(json);
			free(chunk.memory);
			curl_slist_free_all(headers);
			curl_easy_cleanup(curl);
			return 1;
		}

		cJSON *data = cJSON_GetObjectItem(json, "data");

		if (cJSON_IsArray(data)) {
			int count = cJSON_GetArraySize(data);
			if (count > 1) {
				printf("Fetched %d new email(s)\n", count);
			} else if (count == 1) {
				printf("Fetched a new email\n");
			} else {
				printf("No new emails for now, nya~\n");
			}

			cJSON *item = NULL;
			long current_id = last_id;
			cJSON_ArrayForEach(item, data) {
				long email_id = cJSON_GetObjectItem(item, "emailId")->valueint;
				char *sender = cJSON_GetObjectItem(item, "sendEmail")->valuestring;
				char *subject = cJSON_GetObjectItem(item, "subject")->valuestring;
				cJSON *text = cJSON_GetObjectItem(item, "text");

				char subject_crop[128], preview[2100], msg[4096];

				// 裁剪主题
				crop_copy(subject_crop, subject, 100);

				// 裁剪正文
				if (text->type == cJSON_NULL) {
					char *content = cJSON_GetObjectItem(item, "content")->valuestring;
					strip_html(content);
					crop_copy(preview, content, 2048);
				} else {
					crop_copy(preview, text->valuestring, 2048);
				}

				snprintf(msg, sizeof(msg), "📩 *New Email!*\n\n*From:* %s\n*Subject:* %s\n*Preview:*\n%s", 
				         sender, subject_crop, preview);

				send_tg_msg(msg);

				// 更新 ID
				if (email_id > current_id) current_id = email_id;
			}
			if (current_id > last_id) {
				fp = fopen(LAST_ID_FILE, "w");
				if (fp) {
					fprintf(fp, "%ld", current_id);
					fclose(fp);
				} else {
					fprintf(stderr, "Warning: Failed to update %s\n", LAST_ID_FILE);
				}
			}
			// 成功请求后，清除旧的错误记录
			remove(LAST_ERR_FILE);
		}
		cJSON_Delete(json);
	} else {
		fprintf(stderr, "CURL Error: %s\n", curl_easy_strerror(res));
		free(chunk.memory);
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		return 1;
	}

	free(chunk.memory);
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return 0;
}
